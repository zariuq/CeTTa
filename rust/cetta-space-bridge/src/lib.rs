//! C FFI bridge between CeTTa and the MORK/PathMap substrate.
//!
//! The exported surface is intentionally split into a few families:
//! - space lifecycle, mutation, algebra, and query entry points
//! - cursor/product-cursor/overlay-cursor read-side inspection
//! - program/context helpers for ACT-oriented execution
//! - byte/status packet ownership helpers for the C side
//!
//! Panic boundaries are normalized at the extern seam: status/buffer entry points return
//! `MorkStatus`/`MorkBuffer` error packets, while pointer-returning constructors fall back to null.
//! Raw pointers are interpreted only through the typed bridge helpers below so that null checking
//! and lifetime assumptions stay centralized.

use cetta_pathmap_adapter::{OverlayZipper, ZipperSnapshotExt};
use mork::space::{ParDataParser, Space};
use mork_expr::{Expr, ExprEnv, ExprZipper, Tag, item_byte, maybe_byte_item, serialize};
use mork_frontend::bytestring_parser::{Context, Parser, ParserError};
use pathmap::PathMap;
use pathmap::ring::AlgebraicStatus;
use pathmap::zipper::{
    ProductZipper, Zipper, ZipperIteration, ZipperMoving, ZipperProduct,
    ZipperSubtries, ZipperWriting,
};
use std::collections::{BTreeMap, HashMap};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr::{self, slice_from_raw_parts_mut};
use std::sync::{Mutex, OnceLock};

mod counted_pathmap;

#[repr(C)]
pub struct MorkSpace {
    _private: [u8; 0],
}

#[repr(C)]
pub struct MorkProgram {
    _private: [u8; 0],
}

#[repr(C)]
pub struct MorkContext {
    _private: [u8; 0],
}

#[repr(C)]
pub struct MorkCursor {
    _private: [u8; 0],
}

#[repr(C)]
pub struct MorkProductCursor {
    _private: [u8; 0],
}

#[repr(C)]
pub struct MorkOverlayCursor {
    _private: [u8; 0],
}

struct BridgeSpace {
    inner: Space,
    storage_mode: BridgeStorageMode,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
enum BridgeStorageMode {
    // Raw expression storage for explicit lib_mork / `mork:` surfaces.
    RawExprs,
    // CeTTa-owned counted storage for generic `(new-space pathmap)`.
    CountedPathmap,
}

struct BridgeProgram {
    expr_chunks: Vec<Vec<u8>>,
    expr_count: u64,
}

struct BridgeContext {
    inner: Space,
    program_chunks: Vec<Vec<u8>>,
}

struct BridgeCursor {
    snapshot: PathMap<()>,
    path: Vec<u8>,
}

struct BridgeProductCursor {
    snapshots: Vec<PathMap<()>>,
    path: Vec<u8>,
}

struct BridgeOverlayCursor {
    base: PathMap<()>,
    overlay: PathMap<()>,
    path: Vec<u8>,
}

const QUERY_ONLY_V2_MAGIC: u32 = 0x4354_4252;
const QUERY_ONLY_V2_VERSION: u16 = 2;
const QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY: u16 = 1 << 0;
const QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES: u16 = 1 << 1;
#[cfg(feature = "pathmap-space")]
const MULTI_REF_V3_VERSION: u16 = 3;
#[cfg(feature = "pathmap-space")]
const MULTI_REF_V3_FLAG_QUERY_KEYS_ONLY: u16 = 1 << 0;
#[cfg(feature = "pathmap-space")]
const MULTI_REF_V3_FLAG_RAW_EXPR_BYTES: u16 = 1 << 1;
#[cfg(feature = "pathmap-space")]
const MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES: u16 = 1 << 3;
#[repr(i32)]
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum MorkStatusCode {
    Ok = 0,
    Null = 1,
    Parse = 2,
    Panic = 3,
    Internal = 4,
}

#[repr(C)]
#[derive(Debug)]
pub struct MorkStatus {
    pub code: i32,
    pub value: u64,
    pub message: *mut u8,
    pub message_len: usize,
}

#[repr(C)]
#[derive(Debug)]
pub struct MorkBuffer {
    pub code: i32,
    pub data: *mut u8,
    pub len: usize,
    pub count: u32,
    pub message: *mut u8,
    pub message_len: usize,
}

impl Default for MorkStatus {
    fn default() -> Self {
        Self::panic("panic across MORK bridge")
    }
}

impl Default for MorkBuffer {
    fn default() -> Self {
        Self::panic("panic across MORK bridge")
    }
}

impl MorkStatus {
    fn ok(value: u64) -> Self {
        Self {
            code: MorkStatusCode::Ok as i32,
            value,
            message: ptr::null_mut(),
            message_len: 0,
        }
    }

    fn err(code: MorkStatusCode, msg: impl Into<Vec<u8>>) -> Self {
        let (message, message_len) = boxed_bytes_into_raw(msg.into());
        Self {
            code: code as i32,
            value: 0,
            message,
            message_len,
        }
    }

    fn panic(msg: impl Into<Vec<u8>>) -> Self {
        Self::err(MorkStatusCode::Panic, msg)
    }
}

impl MorkBuffer {
    fn ok(data: Vec<u8>, count: u32) -> Self {
        let (ptr, len) = boxed_bytes_into_raw(data);
        Self {
            code: MorkStatusCode::Ok as i32,
            data: ptr,
            len,
            count,
            message: ptr::null_mut(),
            message_len: 0,
        }
    }

    fn err(code: MorkStatusCode, msg: impl Into<Vec<u8>>) -> Self {
        let (message, message_len) = boxed_bytes_into_raw(msg.into());
        Self {
            code: code as i32,
            data: ptr::null_mut(),
            len: 0,
            count: 0,
            message,
            message_len,
        }
    }

    fn panic(msg: impl Into<Vec<u8>>) -> Self {
        Self::err(MorkStatusCode::Panic, msg)
    }
}

fn boxed_bytes_into_raw(bytes: Vec<u8>) -> (*mut u8, usize) {
    if bytes.is_empty() {
        return (ptr::null_mut(), 0);
    }
    let boxed = bytes.into_boxed_slice();
    let len = boxed.len();
    let raw = Box::into_raw(boxed) as *mut u8;
    (raw, len)
}

unsafe fn free_boxed_bytes(data: *mut u8, len: usize) {
    if !data.is_null() {
        // SAFETY: `data,len` must come from `boxed_bytes_into_raw`, which allocates a boxed slice
        // with exactly this element count. The null case is excluded above.
        unsafe {
            drop(Box::from_raw(slice_from_raw_parts_mut(data, len)));
        }
    }
}

fn panic_message(payload: Box<dyn std::any::Any + Send>) -> String {
    if let Some(s) = payload.downcast_ref::<&str>() {
        (*s).to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "panic across MORK bridge".to_string()
    }
}

/// Runs one extern-facing closure and converts unwinds into the type's default error packet.
fn with_catch<T: Default>(f: impl FnOnce() -> T) -> T {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(value) => value,
        Err(payload) => {
            let msg = panic_message(payload);
            let _ = msg;
            T::default()
        }
    }
}

/// Runs one status-returning extern body and translates unwinds into `MorkStatus::panic`.
fn with_catch_status(f: impl FnOnce() -> MorkStatus) -> MorkStatus {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(value) => value,
        Err(payload) => MorkStatus::panic(panic_message(payload).into_bytes()),
    }
}

/// Runs one buffer-returning extern body and translates unwinds into `MorkBuffer::panic`.
fn with_catch_buffer(f: impl FnOnce() -> MorkBuffer) -> MorkBuffer {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(value) => value,
        Err(payload) => MorkBuffer::panic(panic_message(payload).into_bytes()),
    }
}

/// Reinterprets an opaque `MorkSpace` handle as the bridge-owned Rust space wrapper.
///
/// # Safety
/// `space` must either be null or a live pointer previously returned by `mork_space_new`,
/// `mork_space_clone`, `mork_space_join`, `mork_space_meet`, `mork_space_subtract`,
/// `mork_space_restrict`, `mork_cursor_make_map`, or `mork_cursor_make_snapshot_map`, and it
/// must outlive the returned borrow.
unsafe fn bridge_space_ref<'a>(space: *const MorkSpace) -> Result<&'a BridgeSpace, MorkStatus> {
    if space.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkSpace".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `space` is a live bridge-owned allocation of the right type.
    Ok(unsafe { &*(space as *const BridgeSpace) })
}

/// Reinterprets an opaque mutable `MorkSpace` handle as the bridge-owned Rust space wrapper.
///
/// # Safety
/// `space` must either be null or a uniquely owned pointer previously returned by one of the
/// bridge space constructors, and no aliasing mutable or immutable borrows may remain active
/// while the returned reference is used.
unsafe fn bridge_space_mut<'a>(space: *mut MorkSpace) -> Result<&'a mut BridgeSpace, MorkStatus> {
    if space.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkSpace".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `space` is a uniquely owned live bridge allocation.
    Ok(unsafe { &mut *(space as *mut BridgeSpace) })
}

/// Reinterprets an opaque `MorkProgram` handle as the bridge-owned Rust program wrapper.
///
/// # Safety
/// `program` must either be null or a live pointer previously returned by `mork_program_new`,
/// and it must outlive the returned borrow.
unsafe fn bridge_program_ref<'a>(
    program: *const MorkProgram,
) -> Result<&'a BridgeProgram, MorkStatus> {
    if program.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkProgram".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `program` is a live bridge-owned allocation of the right type.
    Ok(unsafe { &*(program as *const BridgeProgram) })
}

/// Reinterprets an opaque mutable `MorkProgram` handle as the bridge-owned Rust program wrapper.
///
/// # Safety
/// `program` must either be null or a uniquely owned pointer previously returned by
/// `mork_program_new`, with no active aliasing borrows during the returned mutable borrow.
unsafe fn bridge_program_mut<'a>(
    program: *mut MorkProgram,
) -> Result<&'a mut BridgeProgram, MorkStatus> {
    if program.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkProgram".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `program` is a uniquely owned live bridge allocation.
    Ok(unsafe { &mut *(program as *mut BridgeProgram) })
}

/// Reinterprets an opaque `MorkContext` handle as the bridge-owned Rust execution wrapper.
///
/// # Safety
/// `context` must either be null or a live pointer previously returned by `mork_context_new`,
/// and it must outlive the returned borrow.
unsafe fn bridge_context_ref<'a>(
    context: *const MorkContext,
) -> Result<&'a BridgeContext, MorkStatus> {
    if context.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkContext".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `context` is a live bridge-owned allocation of the right type.
    Ok(unsafe { &*(context as *const BridgeContext) })
}

/// Reinterprets an opaque mutable `MorkContext` handle as the bridge-owned Rust execution wrapper.
///
/// # Safety
/// `context` must either be null or a uniquely owned pointer previously returned by
/// `mork_context_new`, with no active aliasing borrows during the returned mutable borrow.
unsafe fn bridge_context_mut<'a>(
    context: *mut MorkContext,
) -> Result<&'a mut BridgeContext, MorkStatus> {
    if context.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkContext".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `context` is a uniquely owned live bridge allocation.
    Ok(unsafe { &mut *(context as *mut BridgeContext) })
}

/// Reinterprets an opaque `MorkCursor` handle as the bridge-owned single-space cursor snapshot.
///
/// # Safety
/// `cursor` must either be null or a live pointer previously returned by `mork_cursor_new` or
/// `mork_cursor_fork`, and it must outlive the returned borrow.
unsafe fn bridge_cursor_ref<'a>(cursor: *const MorkCursor) -> Result<&'a BridgeCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkCursor".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `cursor` is a live bridge-owned allocation of the right type.
    Ok(unsafe { &*(cursor as *const BridgeCursor) })
}

/// Reinterprets an opaque mutable `MorkCursor` handle as the bridge-owned single-space cursor snapshot.
///
/// # Safety
/// `cursor` must either be null or a uniquely owned pointer previously returned by
/// `mork_cursor_new` or `mork_cursor_fork`, with no active aliasing borrows during the returned
/// mutable borrow.
unsafe fn bridge_cursor_mut<'a>(
    cursor: *mut MorkCursor,
) -> Result<&'a mut BridgeCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkCursor".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `cursor` is a uniquely owned live bridge allocation.
    Ok(unsafe { &mut *(cursor as *mut BridgeCursor) })
}

/// Reinterprets an opaque `MorkProductCursor` handle as the bridge-owned stitched product cursor.
///
/// # Safety
/// `cursor` must either be null or a live pointer previously returned by `mork_product_cursor_new`,
/// and it must outlive the returned borrow.
unsafe fn bridge_product_cursor_ref<'a>(
    cursor: *const MorkProductCursor,
) -> Result<&'a BridgeProductCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkProductCursor".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `cursor` is a live bridge-owned allocation of the right type.
    Ok(unsafe { &*(cursor as *const BridgeProductCursor) })
}

/// Reinterprets an opaque mutable `MorkProductCursor` handle as the bridge-owned stitched product cursor.
///
/// # Safety
/// `cursor` must either be null or a uniquely owned pointer previously returned by
/// `mork_product_cursor_new`, with no active aliasing borrows during the returned mutable borrow.
unsafe fn bridge_product_cursor_mut<'a>(
    cursor: *mut MorkProductCursor,
) -> Result<&'a mut BridgeProductCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkProductCursor".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `cursor` is a uniquely owned live bridge allocation.
    Ok(unsafe { &mut *(cursor as *mut BridgeProductCursor) })
}

/// Reinterprets an opaque `MorkOverlayCursor` handle as the bridge-owned overlay cursor snapshot.
///
/// # Safety
/// `cursor` must either be null or a live pointer previously returned by `mork_overlay_cursor_new`,
/// and it must outlive the returned borrow.
unsafe fn bridge_overlay_cursor_ref<'a>(
    cursor: *const MorkOverlayCursor,
) -> Result<&'a BridgeOverlayCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkOverlayCursor".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `cursor` is a live bridge-owned allocation of the right type.
    Ok(unsafe { &*(cursor as *const BridgeOverlayCursor) })
}

/// Reinterprets an opaque mutable `MorkOverlayCursor` handle as the bridge-owned overlay cursor snapshot.
///
/// # Safety
/// `cursor` must either be null or a uniquely owned pointer previously returned by
/// `mork_overlay_cursor_new`, with no active aliasing borrows during the returned mutable borrow.
unsafe fn bridge_overlay_cursor_mut<'a>(
    cursor: *mut MorkOverlayCursor,
) -> Result<&'a mut BridgeOverlayCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkOverlayCursor".to_vec(),
        ));
    }
    // SAFETY: the caller guarantees `cursor` is a uniquely owned live bridge allocation.
    Ok(unsafe { &mut *(cursor as *mut BridgeOverlayCursor) })
}

fn cursor_structural_from_focus(
    snapshot: &PathMap<()>,
    path: &[u8],
) -> Result<PathMap<()>, String> {
    let rz = snapshot.read_zipper_at_path(path);
    let subspace = rz
        .try_make_map()
        .ok_or_else(|| "cursor focus does not expose a concrete subtrie".to_string())?;
    Ok(subspace)
}

fn cursor_snapshot_from_focus(snapshot: &PathMap<()>, path: &[u8]) -> Result<PathMap<()>, String> {
    let rz = snapshot.read_zipper_at_path(path);
    let subspace = rz
        .try_make_snapshot_map_ext()
        .ok_or_else(|| "cursor focus does not expose a concrete subtrie".to_string())?;
    Ok(subspace)
}

fn build_product_zipper<'a>(
    bridge: &'a BridgeProductCursor,
) -> Result<ProductZipper<'a, 'a, ()>, MorkStatus> {
    if bridge.snapshots.len() < 2 {
        return Err(MorkStatus::err(
            MorkStatusCode::Internal,
            b"product cursor requires at least two factor snapshots".to_vec(),
        ));
    }
    let mut factors = bridge.snapshots.iter();
    let primary = factors
        .next()
        .expect("product cursor validated non-empty snapshots")
        .read_zipper();
    let secondary = factors
        .map(|snapshot| snapshot.read_zipper())
        .collect::<Vec<_>>();
    let mut prz = ProductZipper::new(primary, secondary);
    prz.descend_to(&bridge.path);
    Ok(prz)
}

fn encode_u64_list(values: &[u64]) -> Vec<u8> {
    let mut out = Vec::with_capacity(values.len() * 8);
    for value in values {
        out.extend_from_slice(&value.to_be_bytes());
    }
    out
}

fn build_overlay_zipper<'a>(
    bridge: &'a BridgeOverlayCursor,
) -> Result<impl ZipperMoving + Zipper + 'a, MorkStatus> {
    let mut oz = OverlayZipper::new(bridge.base.read_zipper(), bridge.overlay.read_zipper());
    oz.descend_to(&bridge.path);
    Ok(oz)
}

fn bridge_space_from_parts(
    space: Space,
    storage_mode: BridgeStorageMode,
) -> *mut MorkSpace {
    let bridge_space = Box::new(BridgeSpace {
        inner: space,
        storage_mode,
    });
    Box::into_raw(bridge_space) as *mut MorkSpace
}

fn bridge_space_from_snapshot(snapshot: PathMap<()>) -> *mut MorkSpace {
    let mut space = Space::new();
    space.btm = snapshot;
    bridge_space_from_parts(space, BridgeStorageMode::RawExprs)
}

fn clone_bridge_space(source: &BridgeSpace) -> *mut MorkSpace {
    let space = Space {
        btm: source.inner.btm.clone(),
        sm: source.inner.sm.clone(),
        mmaps: HashMap::new(),
        z3s: HashMap::new(),
        last_merkleize: source.inner.last_merkleize,
        timing: source.inner.timing,
    };
    bridge_space_from_parts(space, source.storage_mode)
}

fn bridge_uses_counted_storage(bridge: &BridgeSpace) -> bool {
    bridge.storage_mode == BridgeStorageMode::CountedPathmap
}

fn bridge_counted_entries(
    bridge: &BridgeSpace,
) -> Result<Vec<counted_pathmap::CountedEntry>, String> {
    counted_pathmap::counted_entries(&bridge.inner)
}

// Join keeps the destination support and multiplicities honest without any
// mirrored row identity bookkeeping.
fn bridge_space_join_into(dst: &mut BridgeSpace, src: &BridgeSpace) -> AlgebraicStatus {
    let status = {
        let rz = src.inner.btm.read_zipper();
        let mut wz = dst.inner.btm.write_zipper();
        wz.join_into(&rz)
    };
    status
}

// Meet keeps only overlapping structural support.
fn bridge_space_meet_into(dst: &mut BridgeSpace, src: &BridgeSpace) -> AlgebraicStatus {
    let status = {
        let rz = src.inner.btm.read_zipper();
        let mut wz = dst.inner.btm.write_zipper();
        wz.meet_into(&rz, true)
    };
    status
}

// Subtract removes structural support directly from the destination.
fn bridge_space_subtract_into(dst: &mut BridgeSpace, src: &BridgeSpace) -> AlgebraicStatus {
    let status = {
        let rz = src.inner.btm.read_zipper();
        let mut wz = dst.inner.btm.write_zipper();
        wz.subtract_into(&rz, true)
    };
    status
}

// Restrict performs selector-shaped narrowing over structural support.
fn bridge_space_restrict_into(dst: &mut BridgeSpace, src: &BridgeSpace) -> AlgebraicStatus {
    let status = {
        let rz = src.inner.btm.read_zipper();
        let mut wz = dst.inner.btm.write_zipper();
        wz.restrict(&rz)
    };
    status
}

fn clone_then_mutate(
    lhs: &BridgeSpace,
    rhs: &BridgeSpace,
    f: fn(&mut BridgeSpace, &BridgeSpace) -> AlgebraicStatus,
) -> *mut MorkSpace {
    let cloned = clone_bridge_space(lhs);
    let dst = unsafe {
        match bridge_space_mut(cloned) {
            Ok(space) => space,
            Err(_) => {
                mork_space_free(cloned);
                return ptr::null_mut();
            }
        }
    };
    f(dst, rhs);
    cloned
}

fn validate_sexpr_chunk(input: &[u8]) -> Result<usize, String> {
    let mut scratch = Space::new();
    scratch.add_all_sexpr(input)
}

fn dump_program_chunks(chunks: &[Vec<u8>]) -> (Vec<u8>, u32) {
    let mut out = Vec::new();
    let mut count = 0u32;
    for chunk in chunks {
        if chunk.is_empty() {
            continue;
        }
        if !out.is_empty() && !out.ends_with(b"\n") {
            out.push(b'\n');
        }
        out.extend_from_slice(chunk);
        if !out.ends_with(b"\n") {
            out.push(b'\n');
        }
        count = count.saturating_add(1);
    }
    (out, count)
}

fn merged_context_text(bridge: &BridgeContext) -> Result<Vec<u8>, String> {
    let mut merged = Vec::new();
    bridge.inner.dump_all_sexpr(&mut merged)?;
    let (program_text, _) = dump_program_chunks(&bridge.program_chunks);
    if !program_text.is_empty() {
        if !merged.is_empty() && !merged.ends_with(b"\n") {
            merged.push(b'\n');
        }
        merged.extend_from_slice(&program_text);
    }
    Ok(merged)
}

fn build_context_view_space(bridge: &BridgeContext) -> Result<Space, String> {
    let merged = merged_context_text(bridge)?;
    let mut view = Space::new();
    if !merged.is_empty() {
        view.add_all_sexpr(&merged)?;
    }
    Ok(view)
}

fn parse_bridge_path(path: *const u8, len: usize) -> Result<std::path::PathBuf, MorkStatus> {
    if path.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null ACT file path".to_vec(),
        ));
    }
    // SAFETY: callers pass a non-null pointer plus byte length for a UTF-8 filesystem path.
    // The slice is borrowed only for immediate validation and conversion into an owned PathBuf.
    let bytes = unsafe { std::slice::from_raw_parts(path, len) };
    let text = std::str::from_utf8(bytes).map_err(|_| {
        MorkStatus::err(
            MorkStatusCode::Parse,
            b"ACT file path must be valid UTF-8".to_vec(),
        )
    })?;
    Ok(std::path::PathBuf::from(text))
}

fn act_copy_sidecar_path(path: &std::path::Path) -> std::path::PathBuf {
    let mut os = path.as_os_str().to_os_string();
    os.push(".copies");
    std::path::PathBuf::from(os)
}

fn bridge_stored_atom_count(bridge: &BridgeSpace) -> u64 {
    if bridge_uses_counted_storage(bridge) {
        return counted_pathmap::counted_logical_size(&bridge.inner).unwrap_or(0);
    }
    bridge.inner.btm.val_count() as u64
}

// Same-handle algebra is valid at the CeTTa surface. Clone the source view first so the
// native mutator never observes aliased mutable/immutable borrows of the same bridge space.
unsafe fn bridge_space_mutate_from_raw(
    dst: *mut MorkSpace,
    src: *const MorkSpace,
    f: fn(&mut BridgeSpace, &BridgeSpace) -> AlgebraicStatus,
) -> MorkStatus {
    if ptr::eq(dst.cast_const(), src) {
        let cloned = match unsafe { bridge_space_ref(src) } {
            Ok(space) => clone_bridge_space(space),
            Err(err) => return err,
        };
        let result = {
            let dst = match unsafe { bridge_space_mut(dst) } {
                Ok(space) => space,
                Err(err) => {
                    mork_space_free(cloned);
                    return err;
                }
            };
            let src = match unsafe { bridge_space_ref(cloned) } {
                Ok(space) => space,
                Err(err) => {
                    mork_space_free(cloned);
                    return err;
                }
            };
            let _ = f(dst, src);
            MorkStatus::ok(0)
        };
        mork_space_free(cloned);
        return result;
    }

    let dst = match unsafe { bridge_space_mut(dst) } {
        Ok(space) => space,
        Err(err) => return err,
    };
    let src = match unsafe { bridge_space_ref(src) } {
        Ok(space) => space,
        Err(err) => return err,
    };
    let _ = f(dst, src);
    MorkStatus::ok(0)
}

fn read_u32_be_at(input: &[u8], offset: &mut usize) -> Result<u32, String> {
    if input.len().saturating_sub(*offset) < 4 {
        return Err("ACT copy sidecar truncated while reading u32".to_string());
    }
    let value = u32::from_be_bytes([
        input[*offset],
        input[*offset + 1],
        input[*offset + 2],
        input[*offset + 3],
    ]);
    *offset += 4;
    Ok(value)
}

fn apply_act_copy_sidecar(_bridge: &mut BridgeSpace, data: &[u8]) -> Result<(), String> {
    if data.is_empty() {
        return Ok(());
    }
    Err(
        "legacy ACT copy sidecars are no longer supported after the row-provenance purge"
            .to_string(),
    )
}

fn parse_single_expr(space: &mut Space, input: &[u8]) -> Result<Vec<u8>, String> {
    let mut parse_buffer = vec![0u8; input.len().saturating_mul(4).saturating_add(4096)];
    let mut parser = ParDataParser::new(&space.sm);
    let mut context = Context::new(input);
    let mut ez = ExprZipper::new(Expr {
        ptr: parse_buffer.as_mut_ptr(),
    });

    parser
        .sexpr(&mut context, &mut ez)
        .map_err(|e| format!("pattern parse failed: {:?}", e))?;

    skip_trailing(&mut context).map_err(|e| format!("pattern trailing parse failed: {:?}", e))?;
    if context.loc < context.src.len() {
        return Err("pattern contains trailing non-whitespace input".to_string());
    }

    parse_buffer.truncate(ez.loc);
    Ok(parse_buffer)
}

fn parse_expr_chunk(space: &mut Space, input: &[u8]) -> Result<Vec<Vec<u8>>, String> {
    let mut parser = ParDataParser::new(&space.sm);
    let mut context = Context::new(input);
    let mut out = Vec::new();

    loop {
        skip_trailing(&mut context)
            .map_err(|e| format!("chunk trailing parse failed: {:?}", e))?;
        if context.loc >= context.src.len() {
            break;
        }

        let remaining = &context.src[context.loc..];
        let mut parse_buffer = vec![0u8; remaining.len().saturating_mul(4).saturating_add(4096)];
        let mut ez = ExprZipper::new(Expr {
            ptr: parse_buffer.as_mut_ptr(),
        });
        parser
            .sexpr(&mut context, &mut ez)
            .map_err(|e| format!("chunk parse failed: {:?}", e))?;
        parse_buffer.truncate(ez.loc);
        out.push(parse_buffer);
    }

    Ok(out)
}

fn normalize_query_text(input: &[u8]) -> Result<Vec<u8>, String> {
    let text =
        std::str::from_utf8(input).map_err(|_| "query text must be valid UTF-8".to_string())?;
    let trimmed = text.trim_start();
    if trimmed.starts_with("(,") {
        Ok(input.to_vec())
    } else {
        Ok(format!("(, {})", text).into_bytes())
    }
}

fn validate_expr_bytes(input: &[u8]) -> Result<(), String> {
    if input.is_empty() {
        return Err("query expr bytes cannot be empty".to_string());
    }

    let mut pos = 0usize;
    let mut pending = 1usize;
    let mut introduced_vars = 0usize;

    while pending > 0 {
        if pos >= input.len() {
            return Err("query expr bytes truncated".to_string());
        }
        let tag = maybe_byte_item(input[pos])
            .map_err(|byte| format!("query expr bytes contain invalid tag 0x{byte:02x}"))?;
        pos += 1;
        pending -= 1;

        match tag {
            Tag::NewVar => {
                introduced_vars = introduced_vars.saturating_add(1);
            }
            Tag::VarRef(index) => {
                if index as usize >= introduced_vars {
                    return Err(format!(
                        "query expr bytes contain unresolved var ref _{} before introduction",
                        index
                    ));
                }
            }
            Tag::SymbolSize(size) => {
                let end = pos.saturating_add(size as usize);
                if end > input.len() {
                    return Err("query expr bytes truncate a symbol payload".to_string());
                }
                pos = end;
            }
            Tag::Arity(arity) => {
                pending = pending.checked_add(arity as usize).ok_or_else(|| {
                    "query expr bytes overflow expression arity accounting".to_string()
                })?;
            }
        }
    }

    if pos != input.len() {
        return Err("query expr bytes contain trailing data".to_string());
    }
    Ok(())
}

fn parse_expr_batch_packet(input: &[u8]) -> Result<Vec<&[u8]>, String> {
    let mut offset = 0usize;
    let mut exprs = Vec::new();

    while offset < input.len() {
        let expr_len = read_u32_be_at(input, &mut offset)? as usize;
        if input.len().saturating_sub(offset) < expr_len {
            return Err("expr-byte batch truncated while reading expr bytes".to_string());
        }
        let expr = &input[offset..offset + expr_len];
        validate_expr_bytes(expr).map_err(|err| format!("expr-byte batch item invalid: {err}"))?;
        exprs.push(expr);
        offset += expr_len;
    }

    Ok(exprs)
}

fn query_factor_count(pattern_expr: Expr) -> Result<usize, String> {
    let arity = pattern_expr
        .arity()
        .ok_or_else(|| "query bridge expected a compound query expression".to_string())?;
    Ok(arity as usize)
}

fn ensure_query_only_v2_shape(pattern_expr: Expr) -> Result<(), String> {
    let factor_count = query_factor_count(pattern_expr)?;
    if factor_count > 2 {
        return Err(format!(
            "query-only v2 currently supports unary queries only; multi-factor conjunctions need a future multi-ref packet (got {} factors)",
            factor_count - 1
        ));
    }
    Ok(())
}

fn skip_trailing(context: &mut Context<'_>) -> Result<(), ParserError> {
    while context.loc < context.src.len() {
        match context.src[context.loc] {
            b';' => {
                while context.loc < context.src.len() && context.src[context.loc] != b'\n' {
                    context.loc += 1;
                }
                if context.loc < context.src.len() {
                    context.loc += 1;
                }
            }
            b' ' | b'\t' | b'\n' => {
                context.loc += 1;
            }
            _ => break,
        }
    }
    Ok(())
}

fn expr_span_bytes(expr: Expr) -> &'static [u8] {
    unsafe { expr.span().as_ref().unwrap() }
}

fn append_u32_be(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn append_u16_be(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn append_bridge_expr_bytes(space: &Space, out: &mut Vec<u8>, expr: Expr) -> Result<(), String> {
    let sym_table = space.sym_table();
    let mut encoded = Vec::new();
    let mut ez = ExprZipper::new(expr);

    loop {
        match ez.item() {
            Ok(Tag::NewVar) => encoded.push(item_byte(Tag::NewVar)),
            Ok(Tag::VarRef(index)) => encoded.push(item_byte(Tag::VarRef(index))),
            Ok(Tag::Arity(arity)) => encoded.push(item_byte(Tag::Arity(arity))),
            Ok(Tag::SymbolSize(_)) => unreachable!("ExprZipper::item returns Err for symbol bytes"),
            Err(symbol) => {
                let bridge_symbol = if symbol.len() == 8 {
                    let mut handle = [0u8; 8];
                    handle.copy_from_slice(symbol);
                    sym_table.get_bytes(handle).unwrap_or(symbol)
                } else {
                    symbol
                };
                if bridge_symbol.is_empty() || bridge_symbol.len() >= 64 {
                    return Err(format!(
                        "query-only v2 bridge symbol must be 1..63 bytes, got {}",
                        bridge_symbol.len()
                    ));
                }
                encoded.push(item_byte(Tag::SymbolSize(bridge_symbol.len() as u8)));
                encoded.extend_from_slice(bridge_symbol);
            }
        }
        if !ez.next() {
            break;
        }
    }

    append_u32_be(out, encoded.len() as u32);
    out.extend_from_slice(&encoded);
    Ok(())
}

fn append_query_only_v2_header(out: &mut Vec<u8>, row_count: u32) {
    append_u32_be(out, QUERY_ONLY_V2_MAGIC);
    append_u16_be(out, QUERY_ONLY_V2_VERSION);
    append_u16_be(
        out,
        QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY | QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES,
    );
    append_u32_be(out, row_count);
}

#[cfg(feature = "pathmap-space")]
fn append_multi_ref_v3_header(out: &mut Vec<u8>, flags: u16, factor_count: u32, row_count: u32) {
    append_u32_be(out, QUERY_ONLY_V2_MAGIC);
    append_u16_be(out, MULTI_REF_V3_VERSION);
    append_u16_be(out, flags);
    append_u32_be(out, factor_count);
    append_u32_be(out, row_count);
}

static BRIDGE_VAR_NAMES: OnceLock<Mutex<HashMap<(u8, u8), &'static str>>> = OnceLock::new();

fn bridge_var_name(side: u8, index: u8) -> &'static str {
    let cache = BRIDGE_VAR_NAMES.get_or_init(|| Mutex::new(HashMap::new()));
    let mut guard = cache.lock().unwrap();
    if let Some(existing) = guard.get(&(side, index)) {
        return existing;
    }
    let leaked: &'static str = Box::leak(format!("$__mork_b{}_{}", side, index).into_boxed_str());
    guard.insert((side, index), leaked);
    leaked
}

fn serialize_expr_env_bridge(space: &Space, expr_env: ExprEnv) -> Vec<u8> {
    let mut out = Vec::new();
    let sym_table = space.sym_table();
    expr_env.subsexpr().serialize2(
        &mut out,
        |s| {
            let mapped = if s.len() == 8 {
                let mut symbol = [0u8; 8];
                symbol.copy_from_slice(s);
                sym_table
                    .get_bytes(symbol)
                    .map(unsafe { |x| std::str::from_utf8_unchecked(x) })
            } else {
                Some(unsafe { std::str::from_utf8_unchecked(s) })
            };
            unsafe { std::mem::transmute(mapped.expect("bridge symbol bytes should decode")) }
        },
        |index, _is_new| bridge_var_name(expr_env.n, index),
    );
    out
}

fn append_bindings_packet(
    space: &Space,
    out: &mut Vec<u8>,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
    atom_indices: &[u32],
) {
    append_u32_be(out, atom_indices.len() as u32);
    for &idx in atom_indices {
        append_u32_be(out, idx);
    }
    append_u32_be(out, bindings.len() as u32);
    for (&(key_a, key_b), expr_env) in bindings.iter() {
        out.push(key_a);
        out.push(key_b);
        let rendered = serialize_expr_env_bridge(space, *expr_env);
        append_u32_be(out, rendered.len() as u32);
        out.extend_from_slice(&rendered);
    }
}

fn expr_env_is_query_only_safe(expr_env: ExprEnv) -> bool {
    expr_env.subsexpr().is_ground()
}

fn append_query_only_binding_entries(
    space: &Space,
    out: &mut Vec<u8>,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
) -> Result<(), String> {
    append_u32_be(out, bindings.len() as u32);
    for (&(key_side, key_index), expr_env) in bindings.iter() {
        if key_side != 0 {
            return Err(format!(
                "query-only v2 packet rejected candidate-side binding key ({key_side},{key_index})"
            ));
        }
        if !expr_env_is_query_only_safe(*expr_env) {
            return Err(format!(
                "query-only v2 packet rejected non-ground matched-side binding value for query slot {key_index}"
            ));
        }
        append_u16_be(out, key_index as u16);
        out.push(expr_env.n);
        out.push(u8::from(expr_env.subsexpr().is_ground()));
        append_bridge_expr_bytes(space, out, expr_env.subsexpr())?;
    }
    Ok(())
}

#[cfg(feature = "pathmap-space")]
fn append_query_only_binding_signature(
    space: &mut Space,
    out: &mut Vec<u8>,
    bindings: &[(u8, String)],
) -> Result<(), String> {
    append_u32_be(out, bindings.len() as u32);
    for (query_slot, value_text) in bindings {
        append_u16_be(out, *query_slot as u16);
        out.push(0);
        out.push(1);
        let expr_bytes = parse_single_expr(space, value_text.as_bytes())?;
        append_u32_be(out, expr_bytes.len() as u32);
        out.extend_from_slice(&expr_bytes);
    }
    Ok(())
}

fn append_query_only_v2_row(
    space: &Space,
    out: &mut Vec<u8>,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
) -> Result<(), String> {
    append_u32_be(out, 0);
    append_query_only_binding_entries(space, out, bindings)
}

#[cfg(feature = "pathmap-space")]
fn append_multi_ref_counted_multiplicities(
    out: &mut Vec<u8>,
    factor_counts: &[u32],
) -> Result<(), String> {
    for count in factor_counts {
        if *count == 0 {
            return Err("counted multi-ref row resolved to zero multiplicity".to_string());
        }
        append_u32_be(out, *count);
    }
    Ok(())
}

fn append_empty_binding_row(out: &mut Vec<u8>) {
    append_u32_be(out, 0);
    append_u32_be(out, 0);
}

fn query_bindings_packet(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(Vec<u8>, u32), String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(&mut space.inner, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    let mut rows: Vec<Vec<u8>> = Vec::new();

    Space::query_multi(&space.inner.btm, pattern_expr, |result, _matched_expr| {
        let mut row = Vec::new();
        match result {
            Ok(_refs) => append_empty_binding_row(&mut row),
            Err(bindings) => append_bindings_packet(&space.inner, &mut row, &bindings, &[]),
        }
        rows.push(row);
        true
    });

    let row_count = rows.len() as u32;
    let mut packet = Vec::new();
    append_u32_be(&mut packet, row_count);
    for row in rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

fn query_bindings_query_only_v2_packet(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(Vec<u8>, u32), String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(&mut space.inner, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    ensure_query_only_v2_shape(pattern_expr)?;
    let mut error: Option<String> = None;
    let mut row_count = 0u32;
    let mut packet = Vec::new();
    let mut pending_rows: Vec<Vec<u8>> = Vec::new();

    Space::query_multi(&space.inner.btm, pattern_expr, |result, _matched_expr| {
        let mut row = Vec::new();
        let append_result = match result {
            Ok(_refs) => {
                append_empty_binding_row(&mut row);
                Ok(())
            }
            Err(bindings) => append_query_only_v2_row(&space.inner, &mut row, &bindings),
        };
        match append_result {
            Ok(()) => {
                row_count += 1;
                pending_rows.push(row);
                true
            }
            Err(err) => {
                error = Some(err);
                false
            }
        }
    });

    if let Some(err) = error {
        return Err(err);
    }

    append_query_only_v2_header(&mut packet, row_count);
    for row in pending_rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

#[cfg(feature = "pathmap-space")]
fn query_bindings_multi_ref_v3_packet(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(Vec<u8>, u32), String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(&mut space.inner, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    let factor_count = query_factor_count(pattern_expr)?
        .checked_sub(1)
        .ok_or_else(|| "multi-ref v3 expected a wrapped query".to_string())?;
    if factor_count == 0 {
        return Err("multi-ref v3 requires at least one query factor".to_string());
    }

    let mut row_count = 0u32;
    let mut packet = Vec::new();
    let mut pending_rows: Vec<Vec<u8>> = Vec::new();

    if bridge_uses_counted_storage(space) {
        for counted_row in counted_pathmap::counted_query_rows_detailed(&space.inner, &pattern_bytes)?
        {
            let mut row = Vec::new();
            append_multi_ref_counted_multiplicities(&mut row, &counted_row.factor_counts)
            .and_then(|()| {
                append_query_only_binding_signature(&mut space.inner, &mut row, &counted_row.bindings)
            })?;
            row_count += 1;
            pending_rows.push(row);
        }
    } else {
        return Err(
            "multi-ref v3 packets are only available for counted PathMap bridge spaces"
                .to_string(),
        );
    }

    append_multi_ref_v3_header(
        &mut packet,
        MULTI_REF_V3_FLAG_QUERY_KEYS_ONLY
            | MULTI_REF_V3_FLAG_RAW_EXPR_BYTES
            | MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES,
        factor_count as u32,
        row_count,
    );
    for row in pending_rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

#[cfg(not(feature = "pathmap-space"))]
fn query_bindings_multi_ref_v3_packet(
    _space: &mut BridgeSpace,
    _pattern: &[u8],
) -> Result<(Vec<u8>, u32), String> {
    Err("multi-ref v3 packets require the pathmap-space bridge feature".to_string())
}

fn query_debug_text(space: &mut Space, pattern: &[u8]) -> Result<(Vec<u8>, u32), String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(space, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    let mut lines = Vec::new();
    let mut count = 0u32;

    Space::query_multi(&space.btm, pattern_expr, |result, matched_expr| {
        count += 1;
        let mut line = Vec::new();
        line.extend_from_slice(b"match=");
        line.extend_from_slice(serialize(expr_span_bytes(matched_expr)).as_bytes());
        match result {
            Ok(refs) => {
                line.extend_from_slice(b" refs=[");
                for (i, r) in refs.iter().enumerate() {
                    if i != 0 {
                        line.extend_from_slice(b",");
                    }
                    line.extend_from_slice(r.to_string().as_bytes());
                }
                line.extend_from_slice(b"]");
            }
            Err(bindings) => {
                line.extend_from_slice(b" bindings=[");
                for (i, (&(key_a, key_b), expr_env)) in bindings.iter().enumerate() {
                    if i != 0 {
                        line.extend_from_slice(b",");
                    }
                    line.extend_from_slice(b"(");
                    line.extend_from_slice(key_a.to_string().as_bytes());
                    line.extend_from_slice(b",");
                    line.extend_from_slice(key_b.to_string().as_bytes());
                    line.extend_from_slice(b")=");
                    line.extend_from_slice(
                        serialize(expr_span_bytes(expr_env.subsexpr())).as_bytes(),
                    );
                }
                line.extend_from_slice(b"]");
            }
        }
        line.push(b'\n');
        lines.extend_from_slice(&line);
        true
    });

    Ok((lines, count))
}

fn dump_bridge_space_text(bridge: &BridgeSpace) -> Result<(Vec<u8>, u32), String> {
    if bridge_uses_counted_storage(bridge) {
        let mut text = Vec::new();
        let mut count = 0u32;
        for entry in bridge_counted_entries(bridge)? {
            let mut line = Vec::new();
            let mut view = Space::new();
            view.btm.insert(&entry.atom_expr_bytes, ());
            view.dump_all_sexpr(&mut line)?;
            for _ in 0..entry.count {
                text.extend_from_slice(&line);
                count = count.saturating_add(1);
            }
        }
        return Ok((text, count));
    }
    let mut unique_text = Vec::new();
    bridge.inner.dump_all_sexpr(&mut unique_text)?;
    let count = bridge.inner.btm.val_count() as u32;
    Ok((unique_text, count))
}

// === Space lifecycle, mutation, and algebra FFI ===

/// Allocates a fresh bridge-owned MORK space and returns it as an opaque C handle.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_new() -> *mut MorkSpace {
    with_catch(|| {
        let bridge = Box::new(BridgeSpace {
            inner: Space::new(),
            storage_mode: BridgeStorageMode::RawExprs,
        });
        Box::into_raw(bridge) as *mut MorkSpace
    })
}

/// Allocates a fresh bridge-owned counted PathMap space for generic CeTTa pathmap backends.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_new_pathmap() -> *mut MorkSpace {
    with_catch(|| {
        let bridge = Box::new(BridgeSpace {
            inner: Space::new(),
            storage_mode: BridgeStorageMode::CountedPathmap,
        });
        Box::into_raw(bridge) as *mut MorkSpace
    })
}

/// Releases a `MorkSpace` previously created by this bridge. Null is accepted.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_free(space: *mut MorkSpace) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !space.is_null() {
            // SAFETY: `space` must come from one of this bridge's space constructors and is consumed
            // exactly once here. The null case is ignored above.
            drop(Box::from_raw(space as *mut BridgeSpace));
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_clear(space: *mut MorkSpace) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        bridge.inner = Space::new();
        MorkStatus::ok(0)
    })
}

/// Adds one or more UTF-8 S-expression lines into the target space.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_add_text(
    space: *mut MorkSpace,
    text: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if text.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null sexpr text".to_vec());
        }
        // SAFETY: `text` is checked for null above and borrowed only for the duration of parsing.
        let bytes = std::slice::from_raw_parts(text, len);
        if bridge_uses_counted_storage(bridge) {
            match parse_expr_chunk(&mut bridge.inner, bytes) {
                Ok(exprs) => {
                    let mut added = 0u64;
                    for expr_bytes in exprs {
                        if let Err(err) =
                            counted_pathmap::counted_insert_expr(&mut bridge.inner, &expr_bytes)
                        {
                            return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes());
                        }
                        added = added.saturating_add(1);
                    }
                    MorkStatus::ok(added)
                }
                Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
            }
        } else {
            match bridge.inner.add_all_sexpr(bytes) {
                Ok(count) => MorkStatus::ok(count as u64),
                Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
            }
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_add_sexpr(
    space: *mut MorkSpace,
    text: *const u8,
    len: usize,
) -> MorkStatus {
    mork_space_add_text(space, text, len)
}

/// Adds one already-encoded stable bridge expression without going through UTF-8 parsing.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_add_expr_bytes(
    space: *mut MorkSpace,
    expr_bytes: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if expr_bytes.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null expr bytes".to_vec());
        }
        let expr_bytes = std::slice::from_raw_parts(expr_bytes, len);
        if let Err(err) = validate_expr_bytes(expr_bytes) {
            return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes());
        }
        if bridge_uses_counted_storage(bridge) {
            match counted_pathmap::counted_insert_expr(&mut bridge.inner, expr_bytes) {
                Ok(_) => MorkStatus::ok(1),
                Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
            }
        } else {
            bridge.inner.btm.insert(expr_bytes, ());
            MorkStatus::ok(1)
        }
    })
}

/// Adds a packed batch of stable bridge expressions without going through UTF-8 parsing.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_add_expr_bytes_batch(
    space: *mut MorkSpace,
    packet: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if packet.is_null() && len != 0 {
            return MorkStatus::err(MorkStatusCode::Null, b"null expr byte batch".to_vec());
        }
        let packet = if len == 0 {
            &[][..]
        } else {
            std::slice::from_raw_parts(packet, len)
        };
        let exprs = match parse_expr_batch_packet(packet) {
            Ok(exprs) => exprs,
            Err(err) => return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        };
        if exprs.is_empty() {
            return MorkStatus::ok(0);
        }
        if bridge_uses_counted_storage(bridge) {
            for expr in &exprs {
                if let Err(err) = counted_pathmap::counted_insert_expr(&mut bridge.inner, expr) {
                    return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes());
                }
            }
        } else {
            for expr in &exprs {
                bridge.inner.btm.insert(expr, ());
            }
        }
        MorkStatus::ok(exprs.len() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_remove_text(
    space: *mut MorkSpace,
    text: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if text.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null sexpr text".to_vec());
        }
        let bytes = std::slice::from_raw_parts(text, len);
        if bridge_uses_counted_storage(bridge) {
            match parse_expr_chunk(&mut bridge.inner, bytes) {
                Ok(exprs) => {
                    let mut removed = 0u64;
                    for expr_bytes in exprs {
                        match counted_pathmap::counted_remove_one_expr(&mut bridge.inner, &expr_bytes)
                        {
                            Ok(Some(_)) => removed = removed.saturating_add(1),
                            Ok(None) => {}
                            Err(err) => {
                                return MorkStatus::err(
                                    MorkStatusCode::Internal,
                                    err.into_bytes(),
                                );
                            }
                        }
                    }
                    MorkStatus::ok(removed)
                }
                Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
            }
        } else {
            match bridge.inner.remove_all_sexpr(bytes) {
                Ok(count) => MorkStatus::ok(count as u64),
                Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
            }
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_remove_sexpr(
    space: *mut MorkSpace,
    text: *const u8,
    len: usize,
) -> MorkStatus {
    mork_space_remove_text(space, text, len)
}

/// Removes one already-encoded stable bridge expression without going through UTF-8 parsing.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_remove_expr_bytes(
    space: *mut MorkSpace,
    expr_bytes: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if expr_bytes.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null expr bytes".to_vec());
        }
        let expr_bytes = std::slice::from_raw_parts(expr_bytes, len);
        if let Err(err) = validate_expr_bytes(expr_bytes) {
            return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes());
        }
        if bridge_uses_counted_storage(bridge) {
            match counted_pathmap::counted_remove_one_expr(&mut bridge.inner, expr_bytes) {
                Ok(Some(_)) => MorkStatus::ok(1),
                Ok(None) => MorkStatus::ok(0),
                Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
            }
        } else {
            let removed = bridge.inner.btm.remove(expr_bytes).is_some();
            MorkStatus::ok(u64::from(removed))
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_size(space: *const MorkSpace) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        MorkStatus::ok(bridge.inner.btm.val_count() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_unique_size(space: *const MorkSpace) -> MorkStatus {
    mork_space_size(space)
}

/// Advances the raw-expression MORK space for up to `steps` calculus steps.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_step(space: *mut MorkSpace, steps: u64) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if bridge_uses_counted_storage(bridge) {
            return MorkStatus::err(
                MorkStatusCode::Internal,
                b"counted PathMap bridge spaces do not support metta_calculus stepping".to_vec(),
            );
        }
        let capped = if steps > usize::MAX as u64 {
            usize::MAX
        } else {
            steps as usize
        };
        let performed = bridge.inner.metta_calculus(capped);
        MorkStatus::ok(performed as u64)
    })
}

/// Persists the current space to an ACT artifact path.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_dump_act_file(
    space: *mut MorkSpace,
    path: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        let path = match parse_bridge_path(path, len) {
            Ok(path) => path,
            Err(err) => return err,
        };
        let sidecar_path = act_copy_sidecar_path(&path);
        match bridge.inner.backup_tree(&path) {
            Ok(()) => {
                let _ = sidecar_path;
                MorkStatus::ok(bridge_stored_atom_count(bridge))
            }
            Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.to_string().into_bytes()),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_load_act_file(
    space: *mut MorkSpace,
    path: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        let path = match parse_bridge_path(path, len) {
            Ok(path) => path,
            Err(err) => return err,
        };
        let sidecar_path = act_copy_sidecar_path(&path);
        bridge.inner = Space::new();
        match bridge.inner.restore_tree(&path) {
            Ok(()) => match std::fs::read(&sidecar_path) {
                Ok(data) => match apply_act_copy_sidecar(bridge, &data) {
                    Ok(()) => MorkStatus::ok(bridge_stored_atom_count(bridge)),
                    Err(_) => MorkStatus::ok(bridge_stored_atom_count(bridge)),
                },
                Err(err) if err.kind() == std::io::ErrorKind::NotFound => {
                    MorkStatus::ok(bridge_stored_atom_count(bridge))
                }
                Err(err) => MorkStatus::err(
                    MorkStatusCode::Internal,
                    format!("failed to read ACT copy sidecar: {}", err).into_bytes(),
                ),
            },
            Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.to_string().into_bytes()),
        }
    })
}

/// Dumps the current space as UTF-8 S-expression text.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_dump(space: *mut MorkSpace) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(err) => {
                // SAFETY: the error buffer comes from `MorkStatus::err` in `bridge_space_ref` and is
                // consumed exactly once when converting it into a `MorkBuffer` error packet.
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkSpace".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        match dump_bridge_space_text(bridge) {
            Ok((text, count)) => MorkBuffer::ok(text, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

/// Clones one bridge-owned space while preserving the source encoding universe
/// and structural multiplicities.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_clone(space: *const MorkSpace) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        clone_bridge_space(bridge)
    })
}

/// Mutates one bridge-owned space with the PathMap join of the destination and source.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_join_into(dst: *mut MorkSpace, src: *const MorkSpace) -> MorkStatus {
    with_catch_status(|| unsafe { bridge_space_mutate_from_raw(dst, src, bridge_space_join_into) })
}

/// Materializes the PathMap join of two spaces as a fresh bridge-owned space handle.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_join(lhs: *const MorkSpace, rhs: *const MorkSpace) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let lhs = match bridge_space_ref(lhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let rhs = match bridge_space_ref(rhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        clone_then_mutate(lhs, rhs, bridge_space_join_into)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_meet_into(dst: *mut MorkSpace, src: *const MorkSpace) -> MorkStatus {
    with_catch_status(|| unsafe { bridge_space_mutate_from_raw(dst, src, bridge_space_meet_into) })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_meet(lhs: *const MorkSpace, rhs: *const MorkSpace) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let lhs = match bridge_space_ref(lhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let rhs = match bridge_space_ref(rhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        clone_then_mutate(lhs, rhs, bridge_space_meet_into)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_subtract_into(
    dst: *mut MorkSpace,
    src: *const MorkSpace,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        bridge_space_mutate_from_raw(dst, src, bridge_space_subtract_into)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_subtract(
    lhs: *const MorkSpace,
    rhs: *const MorkSpace,
) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let lhs = match bridge_space_ref(lhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let rhs = match bridge_space_ref(rhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        clone_then_mutate(lhs, rhs, bridge_space_subtract_into)
    })
}

/// Materializes valued-prefix restriction as a fresh bridge-owned space handle.
///
/// The right-hand operand acts as a selector over valued encoded prefixes rather than an ordinary
/// logical query pattern.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_restrict_into(
    dst: *mut MorkSpace,
    src: *const MorkSpace,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        bridge_space_mutate_from_raw(dst, src, bridge_space_restrict_into)
    })
}

/// Materializes valued-prefix restriction as a fresh bridge-owned space handle.
///
/// The right-hand operand acts as a selector over valued encoded prefixes rather than an ordinary
/// logical query pattern.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_restrict(
    lhs: *const MorkSpace,
    rhs: *const MorkSpace,
) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let lhs = match bridge_space_ref(lhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let rhs = match bridge_space_ref(rhs) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        clone_then_mutate(lhs, rhs, bridge_space_restrict_into)
    })
}

// === Single-space cursor FFI ===

/// Creates a read-only cursor snapshot over one space.
#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_new(space: *const MorkSpace) -> *mut MorkCursor {
    with_catch(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let cursor = Box::new(BridgeCursor {
            snapshot: bridge.inner.btm.clone(),
            path: Vec::new(),
        });
        Box::into_raw(cursor) as *mut MorkCursor
    })
}

/// Releases a cursor created by `mork_cursor_new` or `mork_cursor_fork`. Null is accepted.
#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_free(cursor: *mut MorkCursor) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !cursor.is_null() {
            // SAFETY: `cursor` must come from this bridge and is consumed exactly once here.
            drop(Box::from_raw(cursor as *mut BridgeCursor));
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_path_exists(cursor: *const MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        MorkStatus::ok(rz.path_exists() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_is_val(cursor: *const MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        MorkStatus::ok(rz.is_val() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_child_count(cursor: *const MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        MorkStatus::ok(rz.child_count() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_path_bytes(cursor: *const MorkCursor) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        MorkBuffer::ok(bridge.path.clone(), bridge.path.len() as u32)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_child_bytes(cursor: *const MorkCursor) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let child_bytes = rz.child_mask().iter().collect::<Vec<u8>>();
        MorkBuffer::ok(child_bytes.clone(), child_bytes.len() as u32)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_val_count(cursor: *const MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        MorkStatus::ok(rz.val_count() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_depth(cursor: *const MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        MorkStatus::ok(bridge.path.len() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_reset(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        bridge.path.clear();
        MorkStatus::ok(0)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_ascend(cursor: *mut MorkCursor, steps: u64) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let old_depth = bridge.path.len();
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let _full = rz.ascend(steps.min(usize::MAX as u64) as usize);
        bridge.path = rz.path().to_vec();
        MorkStatus::ok((bridge.path.len() != old_depth) as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_descend_byte(cursor: *mut MorkCursor, byte: u32) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        if byte > 255 {
            return MorkStatus::err(
                MorkStatusCode::Parse,
                b"cursor byte must be in 0..255".to_vec(),
            );
        }
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.descend_to_existing_byte(byte as u8);
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_descend_index(cursor: *mut MorkCursor, index: u64) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.descend_indexed_byte(index.min(usize::MAX as u64) as usize);
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_descend_first(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.descend_first_byte();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_descend_last(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.descend_last_byte();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

/// Descends while the current cursor focus is unary and non-valued, stopping at the first value or branch.
#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_descend_until(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.descend_until();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_descend_until_max_bytes(
    cursor: *mut MorkCursor,
    max_bytes: u64,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.descend_until_max_bytes(max_bytes.min(usize::MAX as u64) as usize);
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_ascend_until(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.ascend_until();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_ascend_until_branch(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.ascend_until_branch();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_next_sibling_byte(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.to_next_sibling_byte();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_prev_sibling_byte(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.to_prev_sibling_byte();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_next_step(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.to_next_step();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_next_val(cursor: *mut MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.snapshot.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.to_next_val();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_fork(cursor: *const MorkCursor) -> *mut MorkCursor {
    with_catch(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(_) => return ptr::null_mut(),
        };
        let forked = Box::new(BridgeCursor {
            snapshot: bridge.snapshot.clone(),
            path: bridge.path.clone(),
        });
        Box::into_raw(forked) as *mut MorkCursor
    })
}

/// Materializes the focused cursor subtree as a fresh space handle without grafting the focus value onto `[]`.
#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_make_map(cursor: *const MorkCursor) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(_) => return ptr::null_mut(),
        };
        let snapshot = match cursor_structural_from_focus(&bridge.snapshot, &bridge.path) {
            Ok(snapshot) => snapshot,
            Err(_) => return ptr::null_mut(),
        };
        bridge_space_from_snapshot(snapshot)
    })
}

/// Materializes the focused cursor snapshot as a fresh space handle using current snapshot semantics.
#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_make_snapshot_map(cursor: *const MorkCursor) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(_) => return ptr::null_mut(),
        };
        let snapshot = match cursor_snapshot_from_focus(&bridge.snapshot, &bridge.path) {
            Ok(snapshot) => snapshot,
            Err(_) => return ptr::null_mut(),
        };
        bridge_space_from_snapshot(snapshot)
    })
}

// === Product cursor FFI ===

/// Creates a stitched product cursor over two or more spaces.
#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_new(
    spaces: *const *const MorkSpace,
    count: usize,
) -> *mut MorkProductCursor {
    with_catch(|| unsafe {
        if spaces.is_null() || count < 2 {
            return ptr::null_mut();
        }
        // SAFETY: `spaces` is checked for null above and borrowed only long enough to copy the
        // referenced space snapshots into owned PathMap values.
        let refs = std::slice::from_raw_parts(spaces, count);
        let mut snapshots = Vec::with_capacity(count);
        for &space_ptr in refs {
            let bridge = match bridge_space_ref(space_ptr) {
                Ok(space) => space,
                Err(_) => return ptr::null_mut(),
            };
            snapshots.push(bridge.inner.btm.clone());
        }
        let cursor = Box::new(BridgeProductCursor {
            snapshots,
            path: Vec::new(),
        });
        Box::into_raw(cursor) as *mut MorkProductCursor
    })
}

/// Releases a product cursor created by `mork_product_cursor_new`. Null is accepted.
#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_free(cursor: *mut MorkProductCursor) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !cursor.is_null() {
            // SAFETY: `cursor` must come from this bridge and is consumed exactly once here.
            drop(Box::from_raw(cursor as *mut BridgeProductCursor));
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_path_exists(cursor: *const MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        MorkStatus::ok(prz.path_exists() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_is_val(cursor: *const MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        MorkStatus::ok(prz.is_val() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_child_count(cursor: *const MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        MorkStatus::ok(prz.child_count() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_path_bytes(cursor: *const MorkProductCursor) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkProductCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        MorkBuffer::ok(bridge.path.clone(), bridge.path.len() as u32)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_child_bytes(cursor: *const MorkProductCursor) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkProductCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Internal,
                    if err.message.is_null() {
                        b"product cursor construction failed".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let child_bytes = prz.child_mask().iter().collect::<Vec<u8>>();
        MorkBuffer::ok(child_bytes.clone(), child_bytes.len() as u32)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_val_count(cursor: *const MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        MorkStatus::ok(prz.val_count() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_depth(cursor: *const MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        MorkStatus::ok(bridge.path.len() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_factor_count(cursor: *const MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        MorkStatus::ok(prz.factor_count() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_focus_factor(cursor: *const MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        MorkStatus::ok(prz.focus_factor() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_path_indices(cursor: *const MorkProductCursor) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_product_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkProductCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Internal,
                    if err.message.is_null() {
                        b"product cursor construction failed".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let indices = prz
            .path_indices()
            .iter()
            .map(|idx| *idx as u64)
            .collect::<Vec<_>>();
        MorkBuffer::ok(encode_u64_list(&indices), indices.len() as u32)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_reset(cursor: *mut MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        bridge.path.clear();
        MorkStatus::ok(0)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_ascend(
    cursor: *mut MorkProductCursor,
    steps: u64,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let old_path = bridge.path.clone();
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let _full = prz.ascend(steps.min(usize::MAX as u64) as usize);
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok((bridge.path != old_path) as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_descend_byte(
    cursor: *mut MorkProductCursor,
    byte: u32,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        if byte > 255 {
            return MorkStatus::err(
                MorkStatusCode::Parse,
                b"product cursor byte must be in 0..255".to_vec(),
            );
        }
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.descend_to_existing_byte(byte as u8);
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_descend_index(
    cursor: *mut MorkProductCursor,
    index: u64,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.descend_indexed_byte(index.min(usize::MAX as u64) as usize);
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_descend_first(cursor: *mut MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.descend_first_byte();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_descend_last(cursor: *mut MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.descend_last_byte();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

/// Descends the stitched product cursor until it reaches the first value or non-unary branch.
#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_descend_until(cursor: *mut MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.descend_until();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_descend_until_max_bytes(
    cursor: *mut MorkProductCursor,
    max_bytes: u64,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.descend_until_max_bytes(max_bytes.min(usize::MAX as u64) as usize);
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_ascend_until(cursor: *mut MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.ascend_until();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_ascend_until_branch(
    cursor: *mut MorkProductCursor,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.ascend_until_branch();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_next_sibling_byte(
    cursor: *mut MorkProductCursor,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.to_next_sibling_byte();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_prev_sibling_byte(
    cursor: *mut MorkProductCursor,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.to_prev_sibling_byte();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_next_step(cursor: *mut MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.to_next_step();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_product_cursor_next_val(cursor: *mut MorkProductCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_product_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut prz = match build_product_zipper(bridge) {
            Ok(prz) => prz,
            Err(err) => return err,
        };
        let moved = prz.to_next_val();
        let next_path = prz.path().to_vec();
        drop(prz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

// === Overlay cursor FFI ===

/// Creates a left-biased overlay cursor over two spaces.
#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_new(
    base: *const MorkSpace,
    overlay: *const MorkSpace,
) -> *mut MorkOverlayCursor {
    with_catch(|| unsafe {
        let base = match bridge_space_ref(base) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let overlay = match bridge_space_ref(overlay) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let cursor = Box::new(BridgeOverlayCursor {
            base: base.inner.btm.clone(),
            overlay: overlay.inner.btm.clone(),
            path: Vec::new(),
        });
        Box::into_raw(cursor) as *mut MorkOverlayCursor
    })
}

/// Releases an overlay cursor created by `mork_overlay_cursor_new`. Null is accepted.
#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_free(cursor: *mut MorkOverlayCursor) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !cursor.is_null() {
            // SAFETY: `cursor` must come from this bridge and is consumed exactly once here.
            drop(Box::from_raw(cursor as *mut BridgeOverlayCursor));
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_path_exists(cursor: *const MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        MorkStatus::ok(oz.path_exists() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_is_val(cursor: *const MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        MorkStatus::ok(oz.is_val() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_child_count(cursor: *const MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        MorkStatus::ok(oz.child_count() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_path_bytes(cursor: *const MorkOverlayCursor) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_overlay_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkOverlayCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        MorkBuffer::ok(bridge.path.clone(), bridge.path.len() as u32)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_child_bytes(cursor: *const MorkOverlayCursor) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_overlay_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkOverlayCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Internal,
                    if err.message.is_null() {
                        b"overlay cursor construction failed".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let child_bytes = oz.child_mask().iter().collect::<Vec<u8>>();
        MorkBuffer::ok(child_bytes.clone(), child_bytes.len() as u32)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_depth(cursor: *const MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        MorkStatus::ok(bridge.path.len() as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_reset(cursor: *mut MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        bridge.path.clear();
        MorkStatus::ok(0)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_ascend(
    cursor: *mut MorkOverlayCursor,
    steps: u64,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let old_path = bridge.path.clone();
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let _full = oz.ascend(steps.min(usize::MAX as u64) as usize);
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok((bridge.path != old_path) as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_descend_byte(
    cursor: *mut MorkOverlayCursor,
    byte: u32,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        if byte > 255 {
            return MorkStatus::err(
                MorkStatusCode::Parse,
                b"overlay cursor byte must be in 0..255".to_vec(),
            );
        }
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.descend_to_existing_byte(byte as u8);
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_descend_index(
    cursor: *mut MorkOverlayCursor,
    index: u64,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.descend_indexed_byte(index.min(usize::MAX as u64) as usize);
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_descend_first(cursor: *mut MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.descend_first_byte();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_descend_last(cursor: *mut MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.descend_last_byte();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

/// Descends the virtual left-biased overlay trie until the first value or non-unary branch.
#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_descend_until(cursor: *mut MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.descend_until();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_descend_until_max_bytes(
    cursor: *mut MorkOverlayCursor,
    max_bytes: u64,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.descend_until_max_bytes(max_bytes.min(usize::MAX as u64) as usize);
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_ascend_until(cursor: *mut MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.ascend_until();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_ascend_until_branch(
    cursor: *mut MorkOverlayCursor,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.ascend_until_branch();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_next_sibling_byte(
    cursor: *mut MorkOverlayCursor,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.to_next_sibling_byte();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_prev_sibling_byte(
    cursor: *mut MorkOverlayCursor,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.to_prev_sibling_byte();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_overlay_cursor_next_step(cursor: *mut MorkOverlayCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_overlay_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut oz = match build_overlay_zipper(bridge) {
            Ok(oz) => oz,
            Err(err) => return err,
        };
        let moved = oz.to_next_step();
        let next_path = oz.path().to_vec();
        drop(oz);
        bridge.path = next_path;
        MorkStatus::ok(moved as u64)
    })
}

// === Query packet FFI ===

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_query_bindings(
    space: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkSpace".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        if pattern.is_null() {
            return MorkBuffer::err(MorkStatusCode::Null, b"null query pattern".to_vec());
        }
        let pattern = std::slice::from_raw_parts(pattern, len);
        match query_bindings_packet(bridge, pattern) {
            Ok((packet, count)) => MorkBuffer::ok(packet, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Parse, err.into_bytes()),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_query_bindings_query_only_v2(
    space: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkSpace".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        if pattern.is_null() {
            return MorkBuffer::err(MorkStatusCode::Null, b"null query pattern".to_vec());
        }
        let pattern = std::slice::from_raw_parts(pattern, len);
        match query_bindings_query_only_v2_packet(bridge, pattern) {
            Ok((packet, count)) => MorkBuffer::ok(packet, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

/// Returns the multi-reference v3 binding packet used by the CeTTa bridge for authoritative re-matching.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_query_bindings_multi_ref_v3(
    space: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkSpace".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        if pattern.is_null() {
            return MorkBuffer::err(MorkStatusCode::Null, b"null query pattern".to_vec());
        }
        let pattern = std::slice::from_raw_parts(pattern, len);
        match query_bindings_multi_ref_v3_packet(bridge, pattern) {
            Ok((packet, count)) => MorkBuffer::ok(packet, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

/// Returns a human-readable debug trace of one query over the current space.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_query_debug(
    space: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkSpace".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        if pattern.is_null() {
            return MorkBuffer::err(MorkStatusCode::Null, b"null query pattern".to_vec());
        }
        let pattern = std::slice::from_raw_parts(pattern, len);
        match query_debug_text(&mut bridge.inner, pattern) {
            Ok((text, count)) => MorkBuffer::ok(text, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Parse, err.into_bytes()),
        }
    })
}

// === Program/context FFI ===

/// Creates an opaque program buffer for staging ACT-like sexpr chunks before loading them into a context.
#[unsafe(no_mangle)]
pub extern "C" fn mork_program_new() -> *mut MorkProgram {
    with_catch(|| {
        let bridge = BridgeProgram {
            expr_chunks: Vec::new(),
            expr_count: 0,
        };
        Box::into_raw(Box::new(bridge)) as *mut MorkProgram
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_program_free(program: *mut MorkProgram) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !program.is_null() {
            drop(Box::from_raw(program as *mut BridgeProgram));
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_program_clear(program: *mut MorkProgram) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_program_mut(program) {
            Ok(program) => program,
            Err(err) => return err,
        };
        bridge.expr_chunks.clear();
        bridge.expr_count = 0;
        MorkStatus::ok(0)
    })
}

/// Appends validated sexpr chunk text into the staged program buffer.
#[unsafe(no_mangle)]
pub extern "C" fn mork_program_add_sexpr(
    program: *mut MorkProgram,
    text: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_program_mut(program) {
            Ok(program) => program,
            Err(err) => return err,
        };
        if text.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null program sexpr text".to_vec());
        }
        let bytes = std::slice::from_raw_parts(text, len);
        match validate_sexpr_chunk(bytes) {
            Ok(count) => {
                bridge.expr_chunks.push(bytes.to_vec());
                bridge.expr_count = bridge.expr_count.saturating_add(count as u64);
                MorkStatus::ok(count as u64)
            }
            Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_program_size(program: *const MorkProgram) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_program_ref(program) {
            Ok(program) => program,
            Err(err) => return err,
        };
        MorkStatus::ok(bridge.expr_count)
    })
}

/// Dumps the staged program buffer as newline-delimited UTF-8 sexpr text.
#[unsafe(no_mangle)]
pub extern "C" fn mork_program_dump(program: *mut MorkProgram) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_program_mut(program) {
            Ok(program) => program,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkProgram".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        let (text, count) = dump_program_chunks(&bridge.expr_chunks);
        MorkBuffer::ok(text, count)
    })
}

/// Creates an execution context with separate live-space and staged-program storage.
#[unsafe(no_mangle)]
pub extern "C" fn mork_context_new() -> *mut MorkContext {
    with_catch(|| {
        let bridge = BridgeContext {
            inner: Space::new(),
            program_chunks: Vec::new(),
        };
        Box::into_raw(Box::new(bridge)) as *mut MorkContext
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_context_free(context: *mut MorkContext) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !context.is_null() {
            drop(Box::from_raw(context as *mut BridgeContext));
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_context_clear(context: *mut MorkContext) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_context_mut(context) {
            Ok(context) => context,
            Err(err) => return err,
        };
        bridge.inner = Space::new();
        bridge.program_chunks.clear();
        MorkStatus::ok(0)
    })
}

/// Loads staged program chunks into the context without executing them yet.
#[unsafe(no_mangle)]
pub extern "C" fn mork_context_load_program(
    context: *mut MorkContext,
    program: *const MorkProgram,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge_ctx = match bridge_context_mut(context) {
            Ok(context) => context,
            Err(err) => return err,
        };
        let bridge_prog = match bridge_program_ref(program) {
            Ok(program) => program,
            Err(err) => return err,
        };
        if bridge_prog.expr_chunks.is_empty() {
            return MorkStatus::ok(0);
        }
        bridge_ctx
            .program_chunks
            .extend(bridge_prog.expr_chunks.iter().cloned());
        MorkStatus::ok(bridge_prog.expr_count)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_context_add_sexpr(
    context: *mut MorkContext,
    text: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_context_mut(context) {
            Ok(context) => context,
            Err(err) => return err,
        };
        if text.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null context sexpr text".to_vec());
        }
        let bytes = std::slice::from_raw_parts(text, len);
        match bridge.inner.add_all_sexpr(bytes) {
            Ok(count) => MorkStatus::ok(count as u64),
            Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_context_remove_sexpr(
    context: *mut MorkContext,
    text: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_context_mut(context) {
            Ok(context) => context,
            Err(err) => return err,
        };
        if text.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null context sexpr text".to_vec());
        }
        let bytes = std::slice::from_raw_parts(text, len);
        match bridge.inner.remove_all_sexpr(bytes) {
            Ok(count) => MorkStatus::ok(count as u64),
            Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_context_size(context: *const MorkContext) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_context_ref(context) {
            Ok(context) => context,
            Err(err) => return err,
        };
        match build_context_view_space(bridge) {
            Ok(view) => MorkStatus::ok(view.btm.val_count() as u64),
            Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

/// Builds the current execution view, runs up to `steps`, and stores the resulting live space back.
#[unsafe(no_mangle)]
pub extern "C" fn mork_context_run(context: *mut MorkContext, steps: u64) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_context_mut(context) {
            Ok(context) => context,
            Err(err) => return err,
        };
        let mut exec_space = match build_context_view_space(bridge) {
            Ok(view) => view,
            Err(err) => return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
        };
        let capped = if steps > usize::MAX as u64 {
            usize::MAX
        } else {
            steps as usize
        };
        let performed = exec_space.metta_calculus(capped);
        bridge.inner = exec_space;
        bridge.program_chunks.clear();
        MorkStatus::ok(performed as u64)
    })
}

/// Dumps the current context view as UTF-8 sexpr text.
#[unsafe(no_mangle)]
pub extern "C" fn mork_context_dump(context: *mut MorkContext) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_context_mut(context) {
            Ok(context) => context,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkContext".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        match build_context_view_space(bridge) {
            Ok(view) => {
                let mut text = Vec::new();
                match view.dump_all_sexpr(&mut text) {
                    Ok(count) => MorkBuffer::ok(text, count as u32),
                    Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
                }
            }
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

// === Byte ownership FFI ===

/// Releases a byte buffer previously returned in `MorkStatus` or `MorkBuffer`.
#[unsafe(no_mangle)]
pub extern "C" fn mork_bytes_free(data: *mut u8, len: usize) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        free_boxed_bytes(data, len);
    }));
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn status_ok(status: &MorkStatus) -> bool {
        status.code == MorkStatusCode::Ok as i32
    }

    fn buffer_ok(buf: &MorkBuffer) -> bool {
        buf.code == MorkStatusCode::Ok as i32
    }

    fn unique_temp_act_path(name: &str) -> std::path::PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        std::env::temp_dir().join(format!("{}_{}_{}.act", name, std::process::id(), nonce))
    }

    #[test]
    fn add_query_remove_debug_smoke() {
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let add = mork_space_add_sexpr(raw, b"(foo a)\n(foo b)\n(bar c)".as_ptr(), 23);
        assert!(status_ok(&add));
        if !add.message.is_null() {
            mork_bytes_free(add.message, add.message_len);
        }

        let size = mork_space_size(raw);
        assert!(status_ok(&size));
        assert_eq!(size.value, 3);

        let query = mork_space_query_debug(raw, b"(foo $x)".as_ptr(), 8);
        assert!(buffer_ok(&query));
        assert_eq!(query.count, 2);
        let text = unsafe { std::slice::from_raw_parts(query.data, query.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("match="));
        assert!(rendered.contains("bindings=["));
        mork_bytes_free(query.data, query.len);

        let remove = mork_space_remove_sexpr(raw, b"(foo a)".as_ptr(), 7);
        assert!(status_ok(&remove));
        if !remove.message.is_null() {
            mork_bytes_free(remove.message, remove.message_len);
        }

        let size2 = mork_space_size(raw);
        assert!(status_ok(&size2));
        assert_eq!(size2.value, 2);

        mork_space_free(raw);
    }

    #[test]
    fn act_dump_load_round_trips_space_text() {
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let add = mork_space_add_sexpr(raw, b"(foo a)\n(bar b)".as_ptr(), 15);
        assert!(status_ok(&add));

        let path = unique_temp_act_path("cetta_space_bridge_roundtrip");
        let path_text = path.to_str().unwrap().as_bytes().to_vec();

        let dumped = mork_space_dump_act_file(raw, path_text.as_ptr(), path_text.len());
        assert!(status_ok(&dumped));
        assert_eq!(dumped.value, 2);

        let cleared = mork_space_clear(raw);
        assert!(status_ok(&cleared));

        let loaded = mork_space_load_act_file(raw, path_text.as_ptr(), path_text.len());
        assert!(status_ok(&loaded));
        assert_eq!(loaded.value, 2);

        let dump = mork_space_dump(raw);
        assert!(buffer_ok(&dump));
        let text = unsafe { std::slice::from_raw_parts(dump.data, dump.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("(foo a)\n"));
        assert!(rendered.contains("(bar b)\n"));
        mork_bytes_free(dump.data, dump.len);

        let _ = std::fs::remove_file(&path);
        let _ = std::fs::remove_file(act_copy_sidecar_path(&path));
        mork_space_free(raw);
    }

    #[test]
    fn add_text_dump_canonicalizes_surface_spacing() {
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let add1 = mork_space_add_text(raw, b"(edge   a   b)".as_ptr(), b"(edge   a   b)".len());
        let add2 = mork_space_add_text(
            raw,
            b"(nest (pair a   (pair b c))   (text \"x y\"))".as_ptr(),
            b"(nest (pair a   (pair b c))   (text \"x y\"))".len(),
        );
        let add3 = mork_space_add_text(
            raw,
            b"(FList (FSDepth 0) (Cons (\"formula\" \"x\") Nil))".as_ptr(),
            b"(FList (FSDepth 0) (Cons (\"formula\" \"x\") Nil))".len(),
        );
        assert!(status_ok(&add1));
        assert!(status_ok(&add2));
        assert!(status_ok(&add3));

        let size = mork_space_size(raw);
        assert!(status_ok(&size));
        assert_eq!(size.value, 3);

        let dump = mork_space_dump(raw);
        assert!(buffer_ok(&dump));
        assert_eq!(dump.count, 3);
        let text = unsafe { std::slice::from_raw_parts(dump.data, dump.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("(edge a b)\n"));
        assert!(rendered.contains("(nest (pair a (pair b c)) (text \"x y\"))\n"));
        assert!(rendered.contains("(FList (FSDepth 0) (Cons (\"formula\" \"x\") Nil))\n"));
        mork_bytes_free(dump.data, dump.len);
        mork_space_free(raw);
    }

    #[test]
    fn expr_bytes_mutation_matches_text_surface_roundtrip() {
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let mut scratch = Space::new();
        let edge = parse_single_expr(&mut scratch, b"(edge a b)").unwrap();
        let nested = parse_single_expr(
            &mut scratch,
            b"(nest (pair a (pair b c)) (text \"x y\"))",
        )
        .unwrap();

        let add1 = mork_space_add_expr_bytes(raw, edge.as_ptr(), edge.len());
        let add2 = mork_space_add_expr_bytes(raw, nested.as_ptr(), nested.len());
        assert!(status_ok(&add1));
        assert!(status_ok(&add2));

        let size = mork_space_size(raw);
        assert!(status_ok(&size));
        assert_eq!(size.value, 2);

        let removed = mork_space_remove_expr_bytes(raw, edge.as_ptr(), edge.len());
        assert!(status_ok(&removed));
        assert_eq!(removed.value, 1);

        let packet = mork_space_query_bindings(raw, b"(edge a b)".as_ptr(), 10);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 0);
        mork_bytes_free(packet.data, packet.len);

        let dump = mork_space_dump(raw);
        assert!(buffer_ok(&dump));
        let text = unsafe { std::slice::from_raw_parts(dump.data, dump.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(!rendered.contains("(edge a b)\n"));
        assert!(rendered.contains("(nest (pair a (pair b c)) (text \"x y\"))\n"));
        mork_bytes_free(dump.data, dump.len);

        mork_space_free(raw);
    }

    #[test]
    fn expr_bytes_batch_mutation_matches_individual_adds() {
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let mut scratch = Space::new();
        let edge = parse_single_expr(&mut scratch, b"(edge a b)").unwrap();
        let nested = parse_single_expr(
            &mut scratch,
            b"(nest (pair a (pair b c)) (text \"x y\"))",
        )
        .unwrap();

        let mut packet = Vec::new();
        packet.extend_from_slice(&(edge.len() as u32).to_be_bytes());
        packet.extend_from_slice(&edge);
        packet.extend_from_slice(&(nested.len() as u32).to_be_bytes());
        packet.extend_from_slice(&nested);

        let add = mork_space_add_expr_bytes_batch(raw, packet.as_ptr(), packet.len());
        assert!(status_ok(&add));
        assert_eq!(add.value, 2);

        let size = mork_space_size(raw);
        assert!(status_ok(&size));
        assert_eq!(size.value, 2);

        let dump = mork_space_dump(raw);
        assert!(buffer_ok(&dump));
        let text = unsafe { std::slice::from_raw_parts(dump.data, dump.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("(edge a b)\n"));
        assert!(rendered.contains("(nest (pair a (pair b c)) (text \"x y\"))\n"));
        mork_bytes_free(dump.data, dump.len);

        mork_space_free(raw);
    }

    #[test]
    fn bindings_packet_starts_with_row_count() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let add1 = mork_space_add_sexpr(raw, b"(pair a b)".as_ptr(), 10);
        let add2 = mork_space_add_sexpr(raw, b"(pair a c)".as_ptr(), 10);
        assert!(status_ok(&add1));
        assert!(status_ok(&add2));
        let packet = mork_space_query_bindings(raw, b"(pair a $x)".as_ptr(), 11);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 2);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert!(data.len() >= 4);
        assert_eq!(u32::from_be_bytes([data[0], data[1], data[2], data[3]]), 2);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn bindings_packet_uses_zero_refs_and_bridge_vars() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let add = mork_space_add_sexpr(raw, b"(pair a (wrap $y))".as_ptr(), 18);
        assert!(status_ok(&add));
        let packet = mork_space_query_bindings(raw, b"(pair a $x)".as_ptr(), 11);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert!(data.len() > 12);
        let row_count = u32::from_be_bytes([data[0], data[1], data[2], data[3]]);
        assert_eq!(row_count, 1);
        let ref_count = u32::from_be_bytes([data[4], data[5], data[6], data[7]]);
        assert_eq!(ref_count, 0);
        let binding_count = u32::from_be_bytes([data[8], data[9], data[10], data[11]]);
        assert_eq!(binding_count, 1);
        let expr_len = u32::from_be_bytes([data[14], data[15], data[16], data[17]]) as usize;
        let expr_text = std::str::from_utf8(&data[18..18 + expr_len]).unwrap();
        assert_eq!(expr_text, "(wrap $__mork_b1_0)");
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_packet_has_header_and_ground_value() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let add = mork_space_add_sexpr(raw, b"(pair a b)".as_ptr(), 10);
        assert!(status_ok(&add));

        let packet = mork_space_query_bindings_query_only_v2(raw, b"(pair a $x)".as_ptr(), 11);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert!(data.len() >= 24);
        assert_eq!(
            u32::from_be_bytes([data[0], data[1], data[2], data[3]]),
            QUERY_ONLY_V2_MAGIC
        );
        assert_eq!(
            u16::from_be_bytes([data[4], data[5]]),
            QUERY_ONLY_V2_VERSION
        );
        assert_eq!(
            u16::from_be_bytes([data[6], data[7]]),
            QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY | QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES
        );
        assert_eq!(
            u32::from_be_bytes([data[8], data[9], data[10], data[11]]),
            1
        );
        assert_eq!(
            u32::from_be_bytes([data[12], data[13], data[14], data[15]]),
            0
        );
        assert_eq!(
            u32::from_be_bytes([data[16], data[17], data[18], data[19]]),
            1
        );
        assert_eq!(u16::from_be_bytes([data[20], data[21]]), 0);
        assert_eq!(data[22], 1);
        assert_eq!(data[23], 1);
        let expr_len = u32::from_be_bytes([data[24], data[25], data[26], data[27]]) as usize;
        assert_eq!(expr_len, 2);
        assert_eq!(data[28], item_byte(Tag::SymbolSize(1)));
        assert_eq!(data[29], b'b');
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_exact_match_has_zero_refs_and_zero_bindings() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let add = mork_space_add_sexpr(raw, b"(dup a)".as_ptr(), 7);
        assert!(status_ok(&add));

        let size = mork_space_size(raw);
        assert!(status_ok(&size));
        assert_eq!(size.value, 1);

        let packet = mork_space_query_bindings_query_only_v2(raw, b"(dup a)".as_ptr(), 7);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert!(data.len() >= 28);
        assert_eq!(
            u32::from_be_bytes([data[0], data[1], data[2], data[3]]),
            QUERY_ONLY_V2_MAGIC
        );
        assert_eq!(
            u16::from_be_bytes([data[4], data[5]]),
            QUERY_ONLY_V2_VERSION
        );
        assert_eq!(
            u32::from_be_bytes([data[8], data[9], data[10], data[11]]),
            1
        );
        assert_eq!(
            u32::from_be_bytes([data[12], data[13], data[14], data[15]]),
            0
        );
        assert_eq!(
            u32::from_be_bytes([data[16], data[17], data[18], data[19]]),
            0
        );
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_rejects_matched_side_variable_values() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let add = mork_space_add_sexpr(raw, b"(pair a (wrap $y))".as_ptr(), 18);
        assert!(status_ok(&add));

        let packet = mork_space_query_bindings_query_only_v2(raw, b"(pair a $x)".as_ptr(), 11);
        assert_eq!(packet.code, MorkStatusCode::Internal as i32);
        let text = unsafe { std::slice::from_raw_parts(packet.message, packet.message_len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("query-only v2 packet rejected"));
        mork_bytes_free(packet.message, packet.message_len);
        mork_space_free(raw);
    }

    #[test]
    fn query_debug_surfaces_candidate_variable_match() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let fact = b"(pair $x $x)";
        let query = b"(pair a a)";
        let add = mork_space_add_sexpr(raw, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

        let packet = mork_space_query_debug(raw, query.as_ptr(), query.len());
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let text = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("match="));
        assert!(rendered.contains("bindings=["));
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_rejects_candidate_side_binding_keys() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let fact = b"(pair $x $x)";
        let query = b"(pair a a)";
        let add = mork_space_add_sexpr(raw, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

        let packet = mork_space_query_bindings_query_only_v2(raw, query.as_ptr(), query.len());
        assert_eq!(packet.code, MorkStatusCode::Internal as i32);
        let text = unsafe { std::slice::from_raw_parts(packet.message, packet.message_len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("candidate-side binding key (1,0)"));
        mork_bytes_free(packet.message, packet.message_len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_accepts_explicit_unary_wrapper() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let fact = b"(pair a b)";
        let query = b"(, (pair a $x))";
        let add = mork_space_add_sexpr(raw, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

        let packet = mork_space_query_bindings_query_only_v2(raw, query.as_ptr(), query.len());
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_rejects_multi_factor_conjunctions_until_multi_ref_packet() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let fact1 = b"(pair a b)";
        let fact2 = b"(pair b c)";
        let query = b"(, (pair a $x) (pair $x c))";
        let add1 = mork_space_add_sexpr(raw, fact1.as_ptr(), fact1.len());
        let add2 = mork_space_add_sexpr(raw, fact2.as_ptr(), fact2.len());
        assert!(status_ok(&add1));
        assert!(status_ok(&add2));

        let packet = mork_space_query_bindings_query_only_v2(raw, query.as_ptr(), query.len());
        assert_eq!(packet.code, MorkStatusCode::Internal as i32);
        let text = unsafe { std::slice::from_raw_parts(packet.message, packet.message_len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("supports unary queries only"));
        assert!(rendered.contains("future multi-ref packet"));
        mork_bytes_free(packet.message, packet.message_len);
        mork_space_free(raw);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn multi_ref_v3_packet_is_unavailable_on_raw_bridge() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let query = b"(, (pair $x $y) (pair $y $z))";

        let packet = mork_space_query_bindings_multi_ref_v3(raw, query.as_ptr(), query.len());
        assert_eq!(packet.code, MorkStatusCode::Internal as i32);
        let text = unsafe { std::slice::from_raw_parts(packet.message, packet.message_len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("counted PathMap bridge spaces"));
        mork_bytes_free(packet.message, packet.message_len);
        mork_space_free(raw);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn multi_ref_v3_packet_reports_direct_factor_multiplicities_and_ground_bindings() {
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let edge_ab = b"(edge a b)";
        let edge_bc = b"(edge b c)";
        let query = b"(, (edge $x $y) (edge $y $z))";

        for _ in 0..2 {
            let add = mork_space_add_sexpr(counted, edge_ab.as_ptr(), edge_ab.len());
            assert!(status_ok(&add));
        }
        for _ in 0..3 {
            let add = mork_space_add_sexpr(counted, edge_bc.as_ptr(), edge_bc.len());
            assert!(status_ok(&add));
        }

        let packet = mork_space_query_bindings_multi_ref_v3(counted, query.as_ptr(), query.len());
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(
            u32::from_be_bytes([data[0], data[1], data[2], data[3]]),
            QUERY_ONLY_V2_MAGIC
        );
        assert_eq!(u16::from_be_bytes([data[4], data[5]]), MULTI_REF_V3_VERSION);
        assert_eq!(
            u16::from_be_bytes([data[6], data[7]]),
            MULTI_REF_V3_FLAG_QUERY_KEYS_ONLY
                | MULTI_REF_V3_FLAG_RAW_EXPR_BYTES
                | MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES
        );
        assert_eq!(
            u32::from_be_bytes([data[8], data[9], data[10], data[11]]),
            2
        );
        assert_eq!(
            u32::from_be_bytes([data[12], data[13], data[14], data[15]]),
            1
        );
        assert_eq!(
            u32::from_be_bytes([data[16], data[17], data[18], data[19]]),
            2
        );
        assert_eq!(
            u32::from_be_bytes([data[20], data[21], data[22], data[23]]),
            3
        );
        assert_eq!(
            u32::from_be_bytes([data[24], data[25], data[26], data[27]]),
            3
        );
        assert_eq!(u16::from_be_bytes([data[28], data[29]]), 0);
        assert_eq!(data[30], 1);
        assert_eq!(data[31], 1);
        assert_eq!(
            u32::from_be_bytes([data[32], data[33], data[34], data[35]]) as usize,
            2
        );
        assert_eq!(data[36], item_byte(Tag::SymbolSize(1)));
        assert_eq!(data[37], b'a');
        assert_eq!(u16::from_be_bytes([data[38], data[39]]), 1);
        assert_eq!(data[41], 1);
        assert_eq!(
            u32::from_be_bytes([data[42], data[43], data[44], data[45]]) as usize,
            2
        );
        assert_eq!(data[46], item_byte(Tag::SymbolSize(1)));
        assert_eq!(data[47], b'b');
        assert_eq!(u16::from_be_bytes([data[48], data[49]]), 2);
        assert_eq!(data[51], 1);
        assert_eq!(
            u32::from_be_bytes([data[52], data[53], data[54], data[55]]) as usize,
            2
        );
        assert_eq!(data[56], item_byte(Tag::SymbolSize(1)));
        assert_eq!(data[57], b'c');
        assert!(data[30] > 0);
        assert!(data[40] > 0);
        assert!(data[50] > 0);
        assert!(data[30] == 2 || data[40] == 2 || data[50] == 2);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(counted);
    }

    #[cfg(not(feature = "pathmap-space"))]
    #[test]
    fn multi_ref_v3_packet_reports_unavailable_without_pathmap_feature() {
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let query = b"(, (pair $x $y) (pair $y $z))";

        let packet = mork_space_query_bindings_multi_ref_v3(raw, query.as_ptr(), query.len());
        assert_eq!(packet.code, MorkStatusCode::Internal as i32);
        let text = unsafe { std::slice::from_raw_parts(packet.message, packet.message_len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("pathmap-space bridge feature"));
        mork_bytes_free(packet.message, packet.message_len);
        mork_space_free(raw);
    }

    #[test]
    fn program_dump_round_trips_added_chunks() {
        const EXEC_RULE: &[u8] =
            b"(exec (0 step) (, (edge $x $y) (edge $y $z)) (O (+ (path $x $z))))";
        let raw = mork_program_new();
        assert!(!raw.is_null());

        let add1 = mork_program_add_sexpr(raw, b"(edge a b)".as_ptr(), 10);
        let add2 = mork_program_add_sexpr(raw, EXEC_RULE.as_ptr(), EXEC_RULE.len());
        assert!(status_ok(&add1));
        assert!(status_ok(&add2));

        let size = mork_program_size(raw);
        assert!(status_ok(&size));
        assert_eq!(size.value, 2);

        let dump = mork_program_dump(raw);
        assert!(buffer_ok(&dump));
        let text = unsafe { std::slice::from_raw_parts(dump.data, dump.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("(edge a b)\n"));
        assert!(rendered.contains("(exec (0 step)"));
        mork_bytes_free(dump.data, dump.len);
        mork_program_free(raw);
    }

    #[test]
    fn context_load_and_run_preserves_loaded_program_and_facts() {
        const EXEC_RULE: &[u8] =
            b"(exec (0 step) (, (edge $x $y) (edge $y $z)) (O (+ (path $x $z))))";
        let program = mork_program_new();
        let context = mork_context_new();
        assert!(!program.is_null());
        assert!(!context.is_null());

        let add_prog = mork_program_add_sexpr(program, EXEC_RULE.as_ptr(), EXEC_RULE.len());
        assert!(status_ok(&add_prog));

        let add_fact1 = mork_context_add_sexpr(context, b"(edge a b)".as_ptr(), 10);
        let add_fact2 = mork_context_add_sexpr(context, b"(edge b c)".as_ptr(), 10);
        assert!(status_ok(&add_fact1));
        assert!(status_ok(&add_fact2));

        let load = mork_context_load_program(context, program);
        assert!(status_ok(&load));
        assert_eq!(load.value, 1);

        let ran = mork_context_run(context, 100);
        assert!(status_ok(&ran));
        assert_eq!(ran.value, 1);

        let dump = mork_context_dump(context);
        assert!(buffer_ok(&dump));
        let text = unsafe { std::slice::from_raw_parts(dump.data, dump.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("(edge a b)\n"));
        assert!(rendered.contains("(edge b c)\n"));
        assert!(rendered.contains("(path a c)\n"));
        assert!(!rendered.contains("(exec (0 step)"));
        mork_bytes_free(dump.data, dump.len);

        mork_context_free(context);
        mork_program_free(program);
    }

    #[test]
    fn direct_space_combined_load_executes_var_binding_rule() {
        const EXEC_RULE: &[u8] =
            b"(exec (0 step) (, (edge $x $y) (edge $y $z)) (O (+ (path $x $z))))";
        let mut space = Space::new();
        let mut input = Vec::new();
        input.extend_from_slice(b"(edge a b)\n");
        input.extend_from_slice(b"(edge b c)\n");
        input.extend_from_slice(EXEC_RULE);
        input.push(b'\n');

        space.add_all_sexpr(&input).unwrap();

        let ran = space.metta_calculus(1);
        assert_eq!(ran, 1);

        let mut text = Vec::new();
        space.dump_all_sexpr(&mut text).unwrap();
        let rendered = std::str::from_utf8(&text).unwrap();
        assert!(rendered.contains("(edge a b)\n"));
        assert!(rendered.contains("(edge b c)\n"));
        assert!(rendered.contains("(path a c)\n"));
        assert!(!rendered.contains("(exec (0 step)"));
    }

    #[test]
    fn ffi_space_step_executes_one_live_space() {
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let input = b"(edge a b)\n(edge b c)\n(exec (0 step) (, (edge $x $y) (edge $y $z)) (O (+ (path $x $z))))\n";
        let add = mork_space_add_sexpr(raw, input.as_ptr(), input.len());
        assert!(status_ok(&add));
        assert_eq!(add.value, 3);

        let ran = mork_space_step(raw, 1);
        assert!(status_ok(&ran));
        assert_eq!(ran.value, 1);

        let dump = mork_space_dump(raw);
        assert!(buffer_ok(&dump));
        let text = unsafe { std::slice::from_raw_parts(dump.data, dump.len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("(edge a b)\n"));
        assert!(rendered.contains("(edge b c)\n"));
        assert!(rendered.contains("(path a c)\n"));
        assert!(!rendered.contains("(exec (0 step)"));

        mork_bytes_free(dump.data, dump.len);
        mork_space_free(raw);
    }

    #[test]
    fn overlay_bridge_builder_preserves_current_focus() {
        let mut base = PathMap::new();
        base.set_val_at([1u8], ());
        let overlay = PathMap::<()>::new();
        let bridge = BridgeOverlayCursor {
            base,
            overlay,
            path: vec![1u8],
        };

        let oz = build_overlay_zipper(&bridge).expect("overlay zipper should build");
        assert_eq!(oz.path(), &[1u8]);
        assert!(oz.path_exists());
        assert!(oz.is_val());
    }

    #[test]
    fn overlay_bridge_builder_does_not_double_descend() {
        let mut base = PathMap::new();
        base.set_val_at([1u8], ());
        base.set_val_at([1u8, 2u8], ());
        let overlay = PathMap::<()>::new();
        let bridge = BridgeOverlayCursor {
            base,
            overlay,
            path: vec![1u8],
        };

        let mut oz = build_overlay_zipper(&bridge).expect("overlay zipper should build");
        assert_eq!(oz.path(), &[1u8]);
        assert!(oz.descend_first_byte());
        assert_eq!(oz.path(), &[1u8, 2u8]);
    }

    #[test]
    fn overlay_cursor_exact_singleton_reports_path_and_value_after_descend_until() {
        let base = mork_space_new();
        let overlay = mork_space_new();
        assert!(!base.is_null());
        assert!(!overlay.is_null());

        let add_base = mork_space_add_sexpr(base, b"(edge a b)".as_ptr(), 10);
        let add_overlay = mork_space_add_sexpr(overlay, b"(edge a b)".as_ptr(), 10);
        assert!(status_ok(&add_base));
        assert!(status_ok(&add_overlay));

        let cursor = mork_overlay_cursor_new(base, overlay);
        assert!(!cursor.is_null());

        let descended = mork_overlay_cursor_descend_until(cursor);
        assert!(status_ok(&descended));
        assert_eq!(descended.value, 1);

        let exists = mork_overlay_cursor_path_exists(cursor);
        let is_val = mork_overlay_cursor_is_val(cursor);
        assert!(status_ok(&exists));
        assert!(status_ok(&is_val));
        assert_eq!(exists.value, 1);
        assert_eq!(is_val.value, 1);

        mork_overlay_cursor_free(cursor);
        mork_space_free(base);
        mork_space_free(overlay);
    }

    #[test]
    fn cursor_snapshot_from_focus_grafts_focus_value_at_root() {
        let mut snapshot = PathMap::new();
        snapshot.set_val_at([1u8], ());
        snapshot.set_val_at([1u8, 2u8], ());

        let subspace =
            cursor_snapshot_from_focus(&snapshot, &[1u8]).expect("snapshot should build");
        assert!(subspace.get_val_at([]).is_some());
        assert!(subspace.get_val_at([2u8]).is_some());
    }

    #[test]
    fn cursor_snapshot_from_focus_does_not_fabricate_focus_value() {
        let mut snapshot = PathMap::new();
        snapshot.create_path([1u8]);
        snapshot.set_val_at([1u8, 2u8], ());

        let subspace =
            cursor_snapshot_from_focus(&snapshot, &[1u8]).expect("snapshot should build");
        assert!(subspace.get_val_at([]).is_none());
        assert!(subspace.get_val_at([2u8]).is_some());
    }

    #[test]
    fn cursor_structural_from_focus_keeps_focus_value_when_it_is_part_of_the_subtrie() {
        let mut snapshot = PathMap::new();
        snapshot.set_val_at([1u8], ());
        snapshot.set_val_at([1u8, 2u8], ());

        let subspace =
            cursor_structural_from_focus(&snapshot, &[1u8]).expect("structural map should build");
        assert!(subspace.get_val_at([]).is_some());
        assert!(subspace.get_val_at([2u8]).is_some());
    }

    #[test]
    fn ffi_cursor_fork_preserves_current_path() {
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let add = mork_space_add_sexpr(raw, b"(edge a b)\n(edge a c)".as_ptr(), 21);
        assert!(status_ok(&add));

        let cursor = mork_cursor_new(raw);
        assert!(!cursor.is_null());
        let descended = mork_cursor_descend_until(cursor);
        assert!(status_ok(&descended));
        assert_eq!(descended.value, 1);

        let original_path = mork_cursor_path_bytes(cursor);
        assert!(buffer_ok(&original_path));
        let original_bytes =
            unsafe { std::slice::from_raw_parts(original_path.data, original_path.len) }.to_vec();
        assert_eq!(
            original_bytes,
            vec![3u8, 196u8, 101u8, 100u8, 103u8, 101u8, 193u8, 97u8, 193u8]
        );
        let original_depth = mork_cursor_depth(cursor);
        assert!(status_ok(&original_depth));
        assert_eq!(original_depth.value, 9);

        let fork = mork_cursor_fork(cursor);
        assert!(!fork.is_null());

        let fork_path = mork_cursor_path_bytes(fork);
        assert!(buffer_ok(&fork_path));
        let fork_bytes =
            unsafe { std::slice::from_raw_parts(fork_path.data, fork_path.len) }.to_vec();
        assert_eq!(fork_bytes, original_bytes);
        let fork_depth = mork_cursor_depth(fork);
        assert!(status_ok(&fork_depth));
        assert_eq!(fork_depth.value, original_depth.value);

        let descended = mork_cursor_descend_first(fork);
        assert!(status_ok(&descended));
        assert_eq!(descended.value, 1);
        let child_path = mork_cursor_path_bytes(fork);
        assert!(buffer_ok(&child_path));
        let child_bytes =
            unsafe { std::slice::from_raw_parts(child_path.data, child_path.len) }.to_vec();
        assert_eq!(
            child_bytes,
            vec![
                3u8, 196u8, 101u8, 100u8, 103u8, 101u8, 193u8, 97u8, 193u8, 98u8
            ]
        );

        let stepped = mork_cursor_next_step(fork);
        assert!(status_ok(&stepped));
        assert_eq!(stepped.value, 1);
        let stepped_path = mork_cursor_path_bytes(fork);
        assert!(buffer_ok(&stepped_path));
        let stepped_bytes =
            unsafe { std::slice::from_raw_parts(stepped_path.data, stepped_path.len) }.to_vec();
        assert_eq!(
            stepped_bytes,
            vec![
                3u8, 196u8, 101u8, 100u8, 103u8, 101u8, 193u8, 97u8, 193u8, 99u8
            ]
        );

        mork_bytes_free(original_path.data, original_path.len);
        mork_bytes_free(fork_path.data, fork_path.len);
        mork_bytes_free(child_path.data, child_path.len);
        mork_bytes_free(stepped_path.data, stepped_path.len);
        mork_cursor_free(fork);
        mork_cursor_free(cursor);
        mork_space_free(raw);
    }
}
