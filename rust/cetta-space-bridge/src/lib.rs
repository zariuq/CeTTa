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
//!
//! Bridge-owned handles rely on caller-managed ownership. Different handles may be used from
//! different threads, and a handle may be transferred between threads, but the same live
//! space/program/context/cursor handle must not be read, mutated, or freed concurrently without an
//! external ownership protocol or synchronization layer.

use cetta_pathmap_adapter::{OverlayZipper, ZipperSnapshotExt};
use cetta_space::{
    CountedEntry, CountedGeneralQueryCursor, FlatCountedCursorStats, FlatCountedIndexStats,
    FlatCountedQueryAdmission, FlatCountedQueryCursor, FlatCountedQueryIndex,
    FlatSemiNaiveQueryCursor, bridge_expr_env_text, bridge_expr_packet_to_bytes, bridge_expr_text,
    bridge_parse_expr_chunk, bridge_parse_single_expr, counted_contains_expr, counted_entries,
    counted_exact_entry, counted_expr_row_packet, counted_factor_candidates,
    counted_insert_expr_cached, counted_insert_expr_count_cached, counted_query_only_packet_rows,
    counted_query_rows_detailed_packet_rows, counted_remove_one_expr_cached, counted_sexpr_text,
    counted_sync_cached_logical_size, counted_unique_size, stable_bridge_expr_bytes,
    stable_bridge_expr_packet_bytes,
};
use mork::space::Space;
use mork_expr::{Expr, ExprEnv, Tag, maybe_byte_item, unify};
use pathmap::PathMap;
use pathmap::ring::AlgebraicStatus;
use pathmap::zipper::{
    ProductZipper, Zipper, ZipperAbsolutePath, ZipperIteration, ZipperMoving, ZipperProduct,
    ZipperSubtries, ZipperWriting,
};
use std::collections::{BTreeMap, HashMap};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr::{self, slice_from_raw_parts_mut};
use std::sync::{Arc, Mutex};

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
pub struct MorkQueryCursor {
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
    counted_logical_size: u64,
    exact_contexts: HashMap<Vec<u8>, BTreeMap<Vec<u8>, u32>>,
    flat_query_index: Arc<FlatCountedQueryIndex>,
    query_replay_cache: Arc<Mutex<QueryReplayCache>>,
    query_revision: u64,
    counted_version: Arc<CountedVersion>,
}

#[derive(Clone)]
struct CountedMutation {
    expr_bytes: Vec<u8>,
    delta: i64,
}

struct CountedVersion {
    parent: Option<Arc<CountedVersion>>,
    changes: Vec<CountedMutation>,
    monotone: bool,
    depth: u64,
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

#[derive(Copy, Clone)]
enum BridgeQueryCursorKind {
    QueryOnlyV2,
    MultiRefV3 { factor_count: u32 },
}

struct BridgeQueryCursor {
    kind: BridgeQueryCursorKind,
    source: BridgeQueryCursorSource,
    pending_row: Option<Vec<u8>>,
    pending_contextual_row: Option<ContextualResidualRow>,
    indexed_setup_stats: Option<FlatCountedIndexStats>,
    indexed_query_revision: u64,
    indexed_has_residual: bool,
    indexed_has_exact_partition: bool,
    indexed_rows_available: bool,
    replay_hit: bool,
    replay_capture: Option<QueryReplayCapture>,
}

const MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY: usize = 4096;

#[derive(Default)]
struct MaterializedQueryRowChunk {
    bytes: Vec<u8>,
    ranges: Vec<(usize, usize)>,
}

impl MaterializedQueryRowChunk {
    fn push(&mut self, row: &[u8]) {
        let start = self.bytes.len();
        self.bytes.extend_from_slice(row);
        self.ranges.push((start, self.bytes.len()));
    }

    fn get(&self, index: usize) -> Option<&[u8]> {
        let &(start, end) = self.ranges.get(index)?;
        self.bytes.get(start..end)
    }
}

#[derive(Default)]
struct MaterializedQueryRows {
    chunks: Vec<MaterializedQueryRowChunk>,
    len: usize,
}

impl MaterializedQueryRows {
    fn push(&mut self, row: &[u8]) {
        let needs_chunk = self
            .chunks
            .last()
            .is_none_or(|chunk| chunk.ranges.len() == MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY);
        if needs_chunk {
            self.chunks.push(MaterializedQueryRowChunk {
                bytes: Vec::new(),
                ranges: Vec::with_capacity(MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY),
            });
        }
        self.chunks
            .last_mut()
            .expect("materialized row chunk must exist after allocation")
            .push(row);
        self.len += 1;
    }

    fn len(&self) -> usize {
        self.len
    }

    fn get(&self, index: usize) -> Option<&[u8]> {
        if index >= self.len {
            return None;
        }
        let chunk = index / MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY;
        let offset = index % MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY;
        self.chunks.get(chunk)?.get(offset)
    }
}

enum BridgeQueryCursorSource {
    Materialized {
        rows: MaterializedQueryRows,
        next_row: usize,
    },
    Flat(FlatCountedQueryCursor),
    IndexedExactResidual {
        exact: FlatCountedQueryCursor,
        exact_done: bool,
        residual_space: Space,
        residual: ContextualResidualQueryCursor,
    },
    GeneralCounted {
        space: Space,
        cursor: CountedGeneralQueryCursor,
    },
    SemiNaive(FlatSemiNaiveQueryCursor),
    Replay {
        rows: Arc<Vec<Vec<u8>>>,
        next_row: usize,
        stats: FlatCountedCursorStats,
    },
}

fn counted_version_root() -> Arc<CountedVersion> {
    Arc::new(CountedVersion {
        parent: None,
        changes: Vec::new(),
        monotone: true,
        depth: 0,
    })
}

const QUERY_REPLAY_MAX_ENTRIES: usize = 64;
const QUERY_REPLAY_MAX_ROWS: usize = 4096;
const QUERY_REPLAY_MAX_BYTES: usize = 4 * 1024 * 1024;

#[derive(Default)]
struct QueryReplayCache {
    entries: BTreeMap<(u64, Vec<u8>), Arc<Vec<Vec<u8>>>>,
    completions: u64,
    hits: u64,
    rows_stored: u64,
}

struct QueryReplayCapture {
    cache: Arc<Mutex<QueryReplayCache>>,
    key: (u64, Vec<u8>),
    rows: Vec<Vec<u8>>,
    bytes: usize,
    eligible: bool,
}

struct BridgeCursor {
    space: Space,
    storage_mode: BridgeStorageMode,
    path: Vec<u8>,
    raw_expr_rows_started: bool,
    counted_expr_rows: Option<BridgeCountedCursorRows>,
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

#[derive(Clone)]
struct BridgeCountedCursorRows {
    entries: Vec<(Vec<u8>, u32)>,
    entry_index: usize,
    emitted_from_entry: u32,
}

const QUERY_ONLY_V2_MAGIC: u32 = 0x4354_4252;
const QUERY_ONLY_V2_VERSION: u16 = 5;
const QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY: u16 = 1 << 0;
const QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES: u16 = 1 << 1;
const QUERY_ONLY_V2_FLAG_WIDE_TOKENS: u16 = 1 << 4;
const CONTEXTUAL_ROWS_WIRE_VERSION: u16 = 6;
const CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION: u16 = 9;
const CONTEXTUAL_EXACT_ROWS_FLAGS: u16 = 0;
const OPEN_VAR_REF_EXACT: u8 = 0;
const OPEN_VAR_REF_QUERY_SLOT: u8 = 1;
const OPEN_VAR_REF_MATCHED_EXACT: u8 = 2;
const OPEN_VAR_REF_MATCHED_INSTANCE: u8 = 3;
const CONTEXTUAL_QUERY_ROWS_FLAGS: u16 = 0;
const CONTEXTUAL_INDEXED_QUERY_ROWS_FLAGS: u16 = 1 << 0;
const BRIDGE_EXPR_TAG_ARITY: u8 = 0x00;
const BRIDGE_EXPR_TAG_SYMBOL: u8 = 0x01;
const BRIDGE_EXPR_TAG_NEWVAR: u8 = 0x02;
const BRIDGE_EXPR_TAG_VARREF: u8 = 0x03;
const MULTI_REF_V3_VERSION: u16 = 6;
const MULTI_REF_V3_FLAG_QUERY_KEYS_ONLY: u16 = 1 << 0;
const MULTI_REF_V3_FLAG_RAW_EXPR_BYTES: u16 = 1 << 1;
const MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES: u16 = 1 << 3;
const MULTI_REF_V3_FLAG_WIDE_TOKENS: u16 = 1 << 4;
const INDEXED_SPACE_STAT_QUERY_REVISION: u32 = 0;
const INDEXED_SPACE_STAT_CATALOG_BUILT: u32 = 1;
const INDEXED_SPACE_STAT_CATALOG_BUILDS: u32 = 2;
const INDEXED_SPACE_STAT_CATALOG_ROWS_SCANNED: u32 = 3;
const INDEXED_SPACE_STAT_ACCESS_PATH_BUILDS: u32 = 4;
const INDEXED_SPACE_STAT_ACCESS_PATH_ROWS_INDEXED: u32 = 5;
const INDEXED_SPACE_STAT_INCREMENTAL_UPDATES: u32 = 6;
const INDEXED_SPACE_STAT_PLAN_BUILDS: u32 = 7;
const INDEXED_SPACE_STAT_PLAN_CACHE_HITS: u32 = 8;
const INDEXED_SPACE_STAT_REPLAY_COMPLETIONS: u32 = 9;
const INDEXED_SPACE_STAT_REPLAY_HITS: u32 = 10;
const INDEXED_SPACE_STAT_REPLAY_ROWS_STORED: u32 = 11;
const INDEXED_CURSOR_STAT_QUERY_REVISION: u32 = 0;
const INDEXED_CURSOR_STAT_CATALOG_BUILDS: u32 = 1;
const INDEXED_CURSOR_STAT_CATALOG_ROWS_SCANNED: u32 = 2;
const INDEXED_CURSOR_STAT_ACCESS_PATH_BUILDS: u32 = 3;
const INDEXED_CURSOR_STAT_ACCESS_PATH_ROWS_INDEXED: u32 = 4;
const INDEXED_CURSOR_STAT_PLAN_BUILDS: u32 = 5;
const INDEXED_CURSOR_STAT_PLAN_CACHE_HITS: u32 = 6;
const INDEXED_CURSOR_STAT_TRIE_SEEKS: u32 = 7;
const INDEXED_CURSOR_STAT_TRIE_DESCENTS: u32 = 8;
const INDEXED_CURSOR_STAT_ROWS_EMITTED: u32 = 9;
const INDEXED_CURSOR_STAT_MAX_FRAME_CELLS: u32 = 10;
const INDEXED_CURSOR_STAT_ROWS_AGGREGATED: u32 = 11;
const INDEXED_CURSOR_STAT_REPLAY_HIT: u32 = 12;
const INDEXED_CURSOR_STAT_HAS_RESIDUAL: u32 = 13;
const INDEXED_CURSOR_STAT_HAS_EXACT_PARTITION: u32 = 14;
const INDEXED_CURSOR_STAT_ROWS_AVAILABLE: u32 = 15;
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
    pub count: u64,
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
    fn ok(data: Vec<u8>, count: u64) -> Self {
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
/// must outlive the returned borrow. The caller must ensure no other thread mutates or frees the
/// same handle while the returned shared borrow is live.
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
/// while the returned reference is used. The caller must ensure exclusive access to the same live
/// handle for the duration of the mutable borrow, including across threads.
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

/// Reinterprets an opaque mutable `MorkQueryCursor` handle as the bridge-owned query stream.
unsafe fn bridge_query_cursor_mut<'a>(
    cursor: *mut MorkQueryCursor,
) -> Result<&'a mut BridgeQueryCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkQueryCursor".to_vec(),
        ));
    }
    Ok(unsafe { &mut *(cursor as *mut BridgeQueryCursor) })
}

/// Reinterprets an opaque `MorkQueryCursor` handle as the bridge-owned query stream.
unsafe fn bridge_query_cursor_ref<'a>(
    cursor: *const MorkQueryCursor,
) -> Result<&'a BridgeQueryCursor, MorkStatus> {
    if cursor.is_null() {
        return Err(MorkStatus::err(
            MorkStatusCode::Null,
            b"null MorkQueryCursor".to_vec(),
        ));
    }
    Ok(unsafe { &*(cursor as *const BridgeQueryCursor) })
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
) -> Result<*mut MorkSpace, String> {
    let counted_logical_size = if storage_mode == BridgeStorageMode::CountedPathmap {
        let mut logical_size = 0u64;
        counted_sync_cached_logical_size(&space, &mut logical_size)?;
        logical_size
    } else {
        space.btm.val_count() as u64
    };
    let bridge_space = Box::new(BridgeSpace {
        inner: space,
        storage_mode,
        counted_logical_size,
        exact_contexts: HashMap::new(),
        flat_query_index: Arc::new(FlatCountedQueryIndex::default()),
        query_replay_cache: Arc::new(Mutex::new(QueryReplayCache::default())),
        query_revision: 0,
        counted_version: counted_version_root(),
    });
    Ok(Box::into_raw(bridge_space) as *mut MorkSpace)
}

fn bridge_space_from_snapshot(snapshot: PathMap<()>) -> Result<*mut MorkSpace, String> {
    let mut space = Space::new();
    space.btm = snapshot;
    bridge_space_from_parts(space, BridgeStorageMode::RawExprs)
}

fn clone_space_inner(source: &Space) -> Space {
    Space {
        btm: source.btm.clone(),
        sm: source.sm.clone(),
        mmaps: HashMap::new(),
        z3s: HashMap::new(),
        last_merkleize: source.last_merkleize,
        timing: source.timing,
    }
}

fn bridge_space_clone_owned(source: &BridgeSpace) -> BridgeSpace {
    BridgeSpace {
        inner: clone_space_inner(&source.inner),
        storage_mode: source.storage_mode,
        counted_logical_size: source.counted_logical_size,
        exact_contexts: source.exact_contexts.clone(),
        flat_query_index: source.flat_query_index.clone(),
        query_replay_cache: source.query_replay_cache.clone(),
        query_revision: source.query_revision,
        counted_version: source.counted_version.clone(),
    }
}

fn clone_bridge_space(source: &BridgeSpace) -> *mut MorkSpace {
    let bridge_space = Box::new(bridge_space_clone_owned(source));
    Box::into_raw(bridge_space) as *mut MorkSpace
}

fn bridge_monotone_delta_space(
    later: &BridgeSpace,
    earlier: &BridgeSpace,
) -> Result<BridgeSpace, String> {
    if !bridge_uses_counted_storage(later) || !bridge_uses_counted_storage(earlier) {
        return Err("monotone delta requires two counted PathMap spaces".to_string());
    }
    if later.counted_version.depth < earlier.counted_version.depth {
        return Err("monotone delta source does not descend from the baseline".to_string());
    }

    let mut accumulated = BTreeMap::<Vec<u8>, u64>::new();
    let mut version = later.counted_version.clone();
    while !Arc::ptr_eq(&version, &earlier.counted_version) {
        if !version.monotone {
            return Err("monotone delta crosses a removal, reset, or opaque mutation".to_string());
        }
        for change in &version.changes {
            if change.delta <= 0 {
                return Err("monotone delta encountered a non-additive change".to_string());
            }
            let delta = u64::try_from(change.delta)
                .map_err(|_| "monotone delta change exceeds u64".to_string())?;
            let count = accumulated.entry(change.expr_bytes.clone()).or_insert(0);
            *count = count
                .checked_add(delta)
                .ok_or_else(|| "monotone delta multiplicity exceeds u64".to_string())?;
        }
        version = version.parent.clone().ok_or_else(|| {
            "monotone delta source does not descend from the baseline".to_string()
        })?;
    }

    let mut delta = BridgeSpace {
        inner: Space::new(),
        storage_mode: BridgeStorageMode::CountedPathmap,
        counted_logical_size: 0,
        exact_contexts: HashMap::new(),
        flat_query_index: Arc::new(FlatCountedQueryIndex::default()),
        query_replay_cache: Arc::new(Mutex::new(QueryReplayCache::default())),
        query_revision: 0,
        counted_version: counted_version_root(),
    };
    for (expr_bytes, count) in accumulated {
        let count = u32::try_from(count).map_err(|_| {
            "monotone delta multiplicity exceeds counted PathMap capacity".to_string()
        })?;
        counted_insert_expr_count_cached(
            &mut delta.inner,
            &expr_bytes,
            count,
            &mut delta.counted_logical_size,
        )?;
    }
    Ok(delta)
}

fn bridge_uses_counted_storage(bridge: &BridgeSpace) -> bool {
    bridge.storage_mode == BridgeStorageMode::CountedPathmap
}

fn bridge_advance_counted_version(
    bridge: &mut BridgeSpace,
    changes: Vec<CountedMutation>,
    monotone: bool,
) {
    if let Some(current) = Arc::get_mut(&mut bridge.counted_version) {
        current.monotone &= monotone;
        current.changes.extend(changes);
        return;
    }
    bridge.counted_version = Arc::new(CountedVersion {
        parent: Some(bridge.counted_version.clone()),
        changes,
        monotone,
        depth: bridge.counted_version.depth.saturating_add(1),
    });
}

fn bridge_reset_flat_query_index(bridge: &mut BridgeSpace) {
    bridge.flat_query_index = Arc::new(FlatCountedQueryIndex::default());
    if let Ok(mut replay) = bridge.query_replay_cache.lock() {
        replay.entries.clear();
    }
    bridge.query_revision = bridge.query_revision.wrapping_add(1);
    if bridge_uses_counted_storage(bridge) {
        bridge_advance_counted_version(bridge, Vec::new(), false);
    }
}

fn bridge_counted_change_details_required(bridge: &BridgeSpace) -> bool {
    bridge.flat_query_index.is_catalog_built()
        || bridge.counted_version.parent.is_some()
        || Arc::strong_count(&bridge.counted_version) > 1
}

fn bridge_note_counted_mutation_without_details(bridge: &mut BridgeSpace) {
    if !bridge_uses_counted_storage(bridge) {
        return;
    }
    if let Some(current) = Arc::get_mut(&mut bridge.counted_version)
        && current.parent.is_none()
    {
        current.changes.clear();
        current.monotone = true;
    }
    bridge.query_revision = bridge.query_revision.wrapping_add(1);
    if let Ok(mut replay) = bridge.query_replay_cache.lock() {
        replay.entries.clear();
    }
}

fn bridge_note_counted_changes(bridge: &mut BridgeSpace, changes: Vec<CountedMutation>) {
    if !bridge_uses_counted_storage(bridge) || changes.is_empty() {
        return;
    }
    if !bridge_counted_change_details_required(bridge) {
        bridge_note_counted_mutation_without_details(bridge);
        return;
    }
    let monotone = changes.iter().all(|change| change.delta > 0);
    let catalog_built = bridge.flat_query_index.is_catalog_built();
    let changed_exprs = catalog_built.then(|| {
        changes
            .iter()
            .map(|change| change.expr_bytes.clone())
            .collect::<Vec<_>>()
    });
    bridge_advance_counted_version(bridge, changes, monotone);
    bridge.query_revision = bridge.query_revision.wrapping_add(1);
    if let Ok(mut replay) = bridge.query_replay_cache.lock() {
        replay.entries.clear();
    }
    if !catalog_built {
        return;
    }
    let update = Arc::make_mut(&mut bridge.flat_query_index)
        .observe_exprs(&bridge.inner, changed_exprs.unwrap_or_default());
    if update.is_err() {
        bridge.flat_query_index = Arc::new(FlatCountedQueryIndex::default());
    }
}

fn bridge_note_counted_single_change(bridge: &mut BridgeSpace, expr: &[u8], delta: i64) {
    if bridge_counted_change_details_required(bridge) {
        bridge_note_counted_changes(bridge, counted_single_change(expr, delta));
    } else {
        bridge_note_counted_mutation_without_details(bridge);
    }
}

fn bridge_counted_insert_exprs_transaction<T: AsRef<[u8]>>(
    bridge: &mut BridgeSpace,
    exprs: &[T],
) -> Result<u64, String> {
    let mut trial_inner = clone_space_inner(&bridge.inner);
    let mut trial_size = bridge.counted_logical_size;
    let track_changes = bridge_counted_change_details_required(bridge);
    let mut changes = Vec::<CountedMutation>::new();
    let mut change_positions = HashMap::<&[u8], usize>::new();
    let mut added = 0u64;

    for expr in exprs {
        let expr_bytes = expr.as_ref();
        counted_insert_expr_cached(&mut trial_inner, expr_bytes, &mut trial_size)?;
        added = added
            .checked_add(1)
            .ok_or_else(|| "counted PathMap batch size overflow".to_string())?;
        if track_changes {
            if let Some(&position) = change_positions.get(expr_bytes) {
                changes[position].delta = changes[position]
                    .delta
                    .checked_add(1)
                    .ok_or_else(|| "counted mutation multiplicity exceeds i64".to_string())?;
            } else {
                change_positions.insert(expr_bytes, changes.len());
                changes.push(CountedMutation {
                    expr_bytes: expr_bytes.to_vec(),
                    delta: 1,
                });
            }
        }
    }

    bridge.inner = trial_inner;
    bridge.counted_logical_size = trial_size;
    if track_changes {
        bridge_note_counted_changes(bridge, changes);
    } else {
        bridge_note_counted_mutation_without_details(bridge);
    }
    Ok(added)
}

fn bridge_counted_remove_exprs_transaction<T: AsRef<[u8]>>(
    bridge: &mut BridgeSpace,
    exprs: &[T],
) -> Result<u64, String>
where
    T: AsRef<[u8]>,
{
    let mut trial_inner = clone_space_inner(&bridge.inner);
    let mut trial_contexts = bridge.exact_contexts.clone();
    let mut trial_size = bridge.counted_logical_size;
    let track_changes = bridge_counted_change_details_required(bridge);
    let mut changes = Vec::<CountedMutation>::new();
    let mut change_positions = HashMap::<&[u8], usize>::new();
    let mut removed = 0u64;

    for expr in exprs {
        let expr_bytes = expr.as_ref();
        if counted_remove_one_expr_cached(&mut trial_inner, expr_bytes, &mut trial_size)?.is_none()
        {
            continue;
        }
        decrement_any_exact_context_count(&mut trial_contexts, expr_bytes);
        removed = removed
            .checked_add(1)
            .ok_or_else(|| "counted PathMap batch size overflow".to_string())?;
        if track_changes {
            if let Some(&position) = change_positions.get(expr_bytes) {
                changes[position].delta = changes[position]
                    .delta
                    .checked_sub(1)
                    .ok_or_else(|| "counted mutation multiplicity exceeds i64".to_string())?;
            } else {
                change_positions.insert(expr_bytes, changes.len());
                changes.push(CountedMutation {
                    expr_bytes: expr_bytes.to_vec(),
                    delta: -1,
                });
            }
        }
    }

    if removed == 0 {
        return Ok(0);
    }
    bridge.inner = trial_inner;
    bridge.exact_contexts = trial_contexts;
    bridge.counted_logical_size = trial_size;
    if track_changes {
        bridge_note_counted_changes(bridge, changes);
    } else {
        bridge_note_counted_mutation_without_details(bridge);
    }
    Ok(removed)
}

fn counted_single_change(expr: &[u8], delta: i64) -> Vec<CountedMutation> {
    vec![CountedMutation {
        expr_bytes: expr.to_vec(),
        delta,
    }]
}

fn bridge_sync_counted_logical_size(bridge: &mut BridgeSpace) -> Result<(), String> {
    if bridge_uses_counted_storage(bridge) {
        counted_sync_cached_logical_size(&bridge.inner, &mut bridge.counted_logical_size)?;
    } else {
        bridge.counted_logical_size = bridge.inner.btm.val_count() as u64;
    }
    Ok(())
}

fn bridge_space_structural_support(space: &BridgeSpace) -> Result<Space, String> {
    let mut support = Space::new();

    if bridge_uses_counted_storage(space) {
        for entry in counted_entries(&space.inner)? {
            support.btm.insert(&entry.atom_expr_bytes, ());
        }
        return Ok(support);
    }

    let mut rz = space.inner.btm.read_zipper();
    while rz.to_next_val() {
        let stable_expr = bridge_stable_transfer_expr_bytes(&space.inner, rz.origin_path())?;
        support.btm.insert(&stable_expr, ());
    }
    Ok(support)
}

fn bridge_space_structural_algebra(
    dst: &mut BridgeSpace,
    src: &BridgeSpace,
    op: impl FnOnce(&mut Space, &Space) -> AlgebraicStatus,
) -> Result<AlgebraicStatus, String> {
    let status = if bridge_uses_counted_storage(dst) || bridge_uses_counted_storage(src) {
        let mut dst_support = bridge_space_structural_support(dst)?;
        let src_support = bridge_space_structural_support(src)?;
        let status = op(&mut dst_support, &src_support);
        dst.inner = dst_support;
        status
    } else {
        op(&mut dst.inner, &src.inner)
    };

    dst.exact_contexts.clear();
    bridge_sync_counted_logical_size(dst)?;
    bridge_reset_flat_query_index(dst);
    Ok(status)
}

// Structural algebra works over PathMap support. Opening contexts describe how
// to project structural variables back into CeTTa VarIds; after algebra those
// presentation identities are no longer justified. Future value/context
// propagation should add explicit composition semantics instead of preserving
// these maps implicitly.
fn bridge_space_join_into(
    dst: &mut BridgeSpace,
    src: &BridgeSpace,
) -> Result<AlgebraicStatus, String> {
    bridge_space_structural_algebra(dst, src, |dst_space, src_space| {
        let rz = src_space.btm.read_zipper();
        let mut wz = dst_space.btm.write_zipper();
        wz.join_into(&rz)
    })
}

// Meet keeps only overlapping structural support and invalidates opening context
// metadata until context composition is modeled explicitly.
fn bridge_space_meet_into(
    dst: &mut BridgeSpace,
    src: &BridgeSpace,
) -> Result<AlgebraicStatus, String> {
    bridge_space_structural_algebra(dst, src, |dst_space, src_space| {
        let rz = src_space.btm.read_zipper();
        let mut wz = dst_space.btm.write_zipper();
        wz.meet_into(&rz, true)
    })
}

// Subtract removes structural support directly from the destination and drops
// exact opening contexts, even when some structural rows remain.
fn bridge_space_subtract_into(
    dst: &mut BridgeSpace,
    src: &BridgeSpace,
) -> Result<AlgebraicStatus, String> {
    bridge_space_structural_algebra(dst, src, |dst_space, src_space| {
        let rz = src_space.btm.read_zipper();
        let mut wz = dst_space.btm.write_zipper();
        wz.subtract_into(&rz, true)
    })
}

// Restrict performs selector-shaped narrowing over structural support; value
// propagation for surviving rows is a separate PathMap semantics TODO.
fn bridge_space_restrict_into(
    dst: &mut BridgeSpace,
    src: &BridgeSpace,
) -> Result<AlgebraicStatus, String> {
    bridge_space_structural_algebra(dst, src, |dst_space, src_space| {
        let rz = src_space.btm.read_zipper();
        let mut wz = dst_space.btm.write_zipper();
        wz.restrict(&rz)
    })
}

struct BridgeLogicalRow {
    source_expr_bytes: Vec<u8>,
    transfer_expr_bytes: Vec<u8>,
    count: u32,
}

fn bridge_stable_transfer_expr_bytes(space: &Space, expr_bytes: &[u8]) -> Result<Vec<u8>, String> {
    let expr = Expr {
        ptr: expr_bytes.as_ptr().cast_mut(),
    };
    stable_bridge_expr_bytes(space, expr)
}

fn bridge_source_logical_rows(src: &BridgeSpace) -> Result<Vec<BridgeLogicalRow>, String> {
    if bridge_uses_counted_storage(src) {
        return counted_entries(&src.inner).map(|entries| {
            entries
                .into_iter()
                .map(|entry| {
                    bridge_stable_transfer_expr_bytes(&src.inner, &entry.atom_expr_bytes).map(
                        |transfer_expr_bytes| BridgeLogicalRow {
                            source_expr_bytes: entry.atom_expr_bytes,
                            transfer_expr_bytes,
                            count: entry.count,
                        },
                    )
                })
                .collect::<Result<Vec<_>, _>>()
        })?;
    }

    let mut rows = Vec::new();
    let mut rz = src.inner.btm.read_zipper();
    while rz.to_next_val() {
        let source_expr_bytes = rz.origin_path().to_vec();
        let transfer_expr_bytes =
            bridge_stable_transfer_expr_bytes(&src.inner, &source_expr_bytes)?;
        rows.push(BridgeLogicalRow {
            source_expr_bytes,
            transfer_expr_bytes,
            count: 1,
        });
    }
    Ok(rows)
}

fn bridge_context_transfer_expr_map<'a>(
    rows: &'a [BridgeLogicalRow],
) -> HashMap<&'a [u8], &'a [u8]> {
    rows.iter()
        .map(|row| {
            (
                row.source_expr_bytes.as_slice(),
                row.transfer_expr_bytes.as_slice(),
            )
        })
        .collect()
}

fn bridge_space_add_logical_rows_from(
    dst: &mut BridgeSpace,
    src: &BridgeSpace,
) -> Result<u64, String> {
    // This routine may partially mutate `dst` before a later row trips a checked
    // arithmetic or decoding error. Public callers therefore run it against a
    // working clone and only publish the clone on success.
    let rows = bridge_source_logical_rows(src)?;
    let mut added = 0u64;

    if bridge_uses_counted_storage(dst) {
        let expr_map = if bridge_uses_counted_storage(src) {
            Some(bridge_context_transfer_expr_map(&rows))
        } else {
            None
        };
        if let Some(expr_map) = &expr_map {
            for (source_expr_bytes, per_expr) in &src.exact_contexts {
                let Some(transfer_expr_bytes) = expr_map.get(source_expr_bytes.as_slice()) else {
                    continue;
                };
                for (context, count) in per_expr {
                    ensure_exact_context_count_can_add(
                        &dst.exact_contexts,
                        transfer_expr_bytes,
                        context,
                        *count,
                    )?;
                }
            }
        }

        for row in &rows {
            counted_insert_expr_count_cached(
                &mut dst.inner,
                &row.transfer_expr_bytes,
                row.count,
                &mut dst.counted_logical_size,
            )?;
            added = added
                .checked_add(u64::from(row.count))
                .ok_or_else(|| "bridge logical row add count overflow".to_string())?;
        }

        if let Some(expr_map) = expr_map {
            for (source_expr_bytes, per_expr) in &src.exact_contexts {
                let Some(transfer_expr_bytes) = expr_map.get(source_expr_bytes.as_slice()) else {
                    continue;
                };
                for (context, count) in per_expr {
                    add_exact_context_count(
                        &mut dst.exact_contexts,
                        transfer_expr_bytes,
                        context,
                        *count,
                    )?;
                }
            }
        }
    } else {
        // Raw MORK/PathMap destinations store structural support: source
        // multiplicity is visited logically, but duplicate paths coalesce and
        // exact opening contexts do not survive the structural boundary.
        for row in rows {
            dst.inner.btm.insert(&row.transfer_expr_bytes, ());
            added = added
                .checked_add(u64::from(row.count))
                .ok_or_else(|| "bridge logical row add count overflow".to_string())?;
        }
        dst.counted_logical_size = dst.inner.btm.val_count() as u64;
        dst.exact_contexts.clear();
    }

    bridge_reset_flat_query_index(dst);
    Ok(added)
}

fn clone_then_mutate(
    lhs: &BridgeSpace,
    rhs: &BridgeSpace,
    f: fn(&mut BridgeSpace, &BridgeSpace) -> Result<AlgebraicStatus, String>,
) -> Result<*mut MorkSpace, String> {
    let cloned = clone_bridge_space(lhs);
    let dst = unsafe {
        match bridge_space_mut(cloned) {
            Ok(space) => space,
            Err(_) => {
                mork_space_free(cloned);
                return Err("failed to borrow cloned bridge space".to_string());
            }
        }
    };
    if let Err(err) = f(dst, rhs) {
        mork_space_free(cloned);
        return Err(err);
    }
    Ok(cloned)
}

fn validate_sexpr_chunk(input: &[u8]) -> Result<usize, String> {
    let mut scratch = Space::new();
    scratch.add_all_sexpr(input)
}

fn checked_packet_count(len: usize, what: &str) -> Result<u64, String> {
    u64::try_from(len).map_err(|_| format!("{what} exceeds u64 packet limit"))
}

fn dump_program_chunks(chunks: &[Vec<u8>]) -> Result<(Vec<u8>, u64), String> {
    let mut out = Vec::new();
    let mut count = 0u64;
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
        count = count
            .checked_add(1)
            .ok_or_else(|| "program chunk count exceeds u64 packet limit".to_string())?;
    }
    Ok((out, count))
}

fn merged_context_text(bridge: &BridgeContext) -> Result<Vec<u8>, String> {
    let mut merged = Vec::new();
    bridge.inner.dump_all_sexpr(&mut merged)?;
    let (program_text, _) = dump_program_chunks(&bridge.program_chunks)?;
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

fn unique_artifact_staging_path(path: &std::path::Path, tag: &str) -> std::path::PathBuf {
    let nonce = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    let mut os = path.as_os_str().to_os_string();
    os.push(format!(".{}.{}.{}", tag, std::process::id(), nonce));
    std::path::PathBuf::from(os)
}

fn remove_path_any_kind(path: &std::path::Path) -> Result<(), String> {
    match std::fs::symlink_metadata(path) {
        Ok(meta) => if meta.is_dir() {
            std::fs::remove_dir_all(path)
        } else {
            std::fs::remove_file(path)
        }
        .map_err(|err| format!("failed to remove path {}: {}", path.display(), err)),
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(err) => Err(format!(
            "failed to inspect path {} for removal: {}",
            path.display(),
            err
        )),
    }
}

fn bridge_space_dump_act_transactional(
    bridge: &BridgeSpace,
    path: &std::path::Path,
) -> Result<(), String> {
    let sidecar_path = act_copy_sidecar_path(path);
    let staged_act_path = unique_artifact_staging_path(path, "cetta-act-staged");
    let staged_sidecar_path = unique_artifact_staging_path(&sidecar_path, "cetta-sidecar-staged");
    let mut staged_sidecar = false;

    if let Err(err) = bridge.inner.backup_tree(&staged_act_path) {
        return Err(err.to_string());
    }

    match std::fs::rename(&sidecar_path, &staged_sidecar_path) {
        Ok(()) => {
            staged_sidecar = true;
        }
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => {}
        Err(err) => {
            let _ = remove_path_any_kind(&staged_act_path);
            return Err(format!("failed to stage stale ACT copy sidecar: {}", err));
        }
    }

    if let Err(err) = std::fs::rename(&staged_act_path, path) {
        let _ = remove_path_any_kind(&staged_act_path);
        if staged_sidecar {
            if let Err(restore_err) = std::fs::rename(&staged_sidecar_path, &sidecar_path) {
                return Err(format!(
                    "failed to publish ACT dump: {}; additionally failed to restore stale ACT copy sidecar: {}",
                    err, restore_err
                ));
            }
        }
        return Err(format!("failed to publish ACT dump: {}", err));
    }

    if staged_sidecar {
        let _ = remove_path_any_kind(&staged_sidecar_path);
    }

    Ok(())
}

fn bridge_space_load_act_replacement(
    storage_mode: BridgeStorageMode,
    path: &std::path::Path,
) -> Result<BridgeSpace, String> {
    let mut loaded = BridgeSpace {
        inner: Space::new(),
        storage_mode,
        counted_logical_size: 0,
        exact_contexts: HashMap::new(),
        flat_query_index: Arc::new(FlatCountedQueryIndex::default()),
        query_replay_cache: Arc::new(Mutex::new(QueryReplayCache::default())),
        query_revision: 0,
        counted_version: counted_version_root(),
    };
    let sidecar_path = act_copy_sidecar_path(path);

    loaded
        .inner
        .restore_tree(path)
        .map_err(|err| err.to_string())?;

    match std::fs::read(&sidecar_path) {
        Ok(data) => {
            apply_act_copy_sidecar(&mut loaded, &data)?;
        }
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => {}
        Err(err) => {
            return Err(format!("failed to read ACT copy sidecar: {}", err));
        }
    }

    bridge_sync_counted_logical_size(&mut loaded)?;
    Ok(loaded)
}

fn bridge_stored_atom_count(bridge: &BridgeSpace) -> u64 {
    if bridge_uses_counted_storage(bridge) {
        return bridge.counted_logical_size;
    }
    bridge.inner.btm.val_count() as u64
}

// Same-handle algebra is valid at the CeTTa surface. Clone the source view first so the
// native mutator never observes aliased mutable/immutable borrows of the same bridge space.
unsafe fn bridge_space_mutate_from_raw(
    dst: *mut MorkSpace,
    src: *const MorkSpace,
    f: fn(&mut BridgeSpace, &BridgeSpace) -> Result<AlgebraicStatus, String>,
) -> MorkStatus {
    let mut cloned_src: *mut MorkSpace = std::ptr::null_mut();
    let src_for_read = if ptr::eq(dst.cast_const(), src) {
        let cloned = match unsafe { bridge_space_ref(src) } {
            Ok(space) => clone_bridge_space(space),
            Err(err) => return err,
        };
        cloned_src = cloned;
        cloned.cast_const()
    } else {
        src
    };

    let dst = match unsafe { bridge_space_mut(dst) } {
        Ok(space) => space,
        Err(err) => {
            if !cloned_src.is_null() {
                mork_space_free(cloned_src);
            }
            return err;
        }
    };
    let src = match unsafe { bridge_space_ref(src_for_read) } {
        Ok(space) => space,
        Err(err) => {
            if !cloned_src.is_null() {
                mork_space_free(cloned_src);
            }
            return err;
        }
    };
    let mut working = bridge_space_clone_owned(dst);
    if let Err(err) = f(&mut working, src) {
        if !cloned_src.is_null() {
            mork_space_free(cloned_src);
        }
        return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes());
    }
    *dst = working;
    if !cloned_src.is_null() {
        mork_space_free(cloned_src);
    }
    MorkStatus::ok(0)
}

unsafe fn bridge_space_add_logical_rows_from_raw(
    dst: *mut MorkSpace,
    src: *const MorkSpace,
) -> MorkStatus {
    let mut cloned_src: *mut MorkSpace = std::ptr::null_mut();
    let src_for_read = if ptr::eq(dst.cast_const(), src) {
        let cloned = match unsafe { bridge_space_ref(src) } {
            Ok(space) => clone_bridge_space(space),
            Err(err) => return err,
        };
        cloned_src = cloned;
        cloned.cast_const()
    } else {
        src
    };

    let dst = match unsafe { bridge_space_mut(dst) } {
        Ok(space) => space,
        Err(err) => {
            if !cloned_src.is_null() {
                mork_space_free(cloned_src);
            }
            return err;
        }
    };
    let src = match unsafe { bridge_space_ref(src_for_read) } {
        Ok(space) => space,
        Err(err) => {
            if !cloned_src.is_null() {
                mork_space_free(cloned_src);
            }
            return err;
        }
    };
    let mut working = bridge_space_clone_owned(dst);
    let status = match bridge_space_add_logical_rows_from(&mut working, src) {
        Ok(added) => {
            *dst = working;
            MorkStatus::ok(added)
        }
        Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
    };
    if !cloned_src.is_null() {
        mork_space_free(cloned_src);
    }
    status
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

fn read_u16_be_at(input: &[u8], offset: &mut usize) -> Result<u16, String> {
    if input.len().saturating_sub(*offset) < 2 {
        return Err("contextual exact context truncated while reading u16".to_string());
    }
    let value = u16::from_be_bytes([input[*offset], input[*offset + 1]]);
    *offset += 2;
    Ok(value)
}

fn read_u8_at(input: &[u8], offset: &mut usize) -> Result<u8, String> {
    let Some(value) = input.get(*offset).copied() else {
        return Err("contextual exact context truncated while reading u8".to_string());
    };
    *offset += 1;
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
    bridge_parse_single_expr(space, input)
}

fn parse_expr_chunk(space: &mut Space, input: &[u8]) -> Result<Vec<Vec<u8>>, String> {
    bridge_parse_expr_chunk(space, input)
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

fn append_u32_be(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn append_u64_be(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn append_u16_be(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn append_bridge_expr_bytes(space: &Space, out: &mut Vec<u8>, expr: Expr) -> Result<(), String> {
    let encoded = stable_bridge_expr_packet_bytes(space, expr)?;
    let encoded_len = u32::try_from(encoded.len())
        .map_err(|_| "bridge expr packet exceeds u32 row length".to_string())?;
    append_u32_be(out, encoded_len);
    out.extend_from_slice(&encoded);
    Ok(())
}

fn bridge_expr_packet_var_count(input: &[u8]) -> Result<u16, String> {
    let mut offset = 0usize;
    let mut pending = 1usize;
    let mut introduced_vars = 0u16;

    while pending > 0 {
        let Some(tag) = input.get(offset).copied() else {
            return Err("contextual expr packet truncated while reading tag".to_string());
        };
        offset += 1;
        pending -= 1;

        match tag {
            BRIDGE_EXPR_TAG_ARITY => {
                let arity = read_u32_be_at(input, &mut offset)? as usize;
                pending = pending.checked_add(arity).ok_or_else(|| {
                    "contextual expr packet overflowed expression arity accounting".to_string()
                })?;
            }
            BRIDGE_EXPR_TAG_SYMBOL => {
                let len = read_u32_be_at(input, &mut offset)? as usize;
                if input.len().saturating_sub(offset) < len {
                    return Err(
                        "contextual expr packet truncated while reading symbol bytes".to_string(),
                    );
                }
                offset += len;
            }
            BRIDGE_EXPR_TAG_NEWVAR => {
                introduced_vars = introduced_vars.checked_add(1).ok_or_else(|| {
                    "contextual expr packet exceeded u16 variable slots".to_string()
                })?;
            }
            BRIDGE_EXPR_TAG_VARREF => {
                let Some(slot) = input.get(offset).copied() else {
                    return Err(
                        "contextual expr packet truncated while reading var ref".to_string()
                    );
                };
                offset += 1;
                if u16::from(slot) >= introduced_vars {
                    return Err(format!(
                        "contextual expr packet references variable slot {slot} before introduction"
                    ));
                }
            }
            other => {
                return Err(format!(
                    "contextual expr packet contains invalid tag 0x{other:02x}"
                ));
            }
        }
    }

    if offset != input.len() {
        return Err("contextual expr packet contains trailing data".to_string());
    }
    Ok(introduced_vars)
}

fn validate_contextual_exact_context(context: &[u8], var_count: u16) -> Result<(), String> {
    let mut offset = 0usize;
    let entry_count = read_u32_be_at(context, &mut offset)?;
    if entry_count != u32::from(var_count) {
        return Err(format!(
            "contextual exact context has {entry_count} entries but expression uses {var_count} variable slots"
        ));
    }

    let mut seen = vec![false; usize::from(var_count)];
    for _ in 0..entry_count {
        let slot = read_u16_be_at(context, &mut offset)?;
        let kind = read_u8_at(context, &mut offset)?;
        let reserved = read_u8_at(context, &mut offset)?;
        if reserved != 0 {
            return Err("contextual exact context entry has nonzero reserved byte".to_string());
        }
        if kind != OPEN_VAR_REF_EXACT {
            return Err(
                "contextual exact rows may only use exact variable context entries".to_string(),
            );
        }
        if slot >= var_count {
            return Err(format!(
                "contextual exact context slot {slot} is outside expression variable range"
            ));
        }
        let seen_slot = &mut seen[usize::from(slot)];
        if *seen_slot {
            return Err(format!("contextual exact context repeats slot {slot}"));
        }
        *seen_slot = true;

        if context.len().saturating_sub(offset) < 8 {
            return Err("contextual exact context truncated while reading VarId".to_string());
        }
        offset += 8;
        let spelling_len = read_u32_be_at(context, &mut offset)? as usize;
        if spelling_len == 0 {
            return Err("contextual exact context contains empty variable spelling".to_string());
        }
        if context.len().saturating_sub(offset) < spelling_len {
            return Err("contextual exact context truncated while reading spelling".to_string());
        }
        std::str::from_utf8(&context[offset..offset + spelling_len])
            .map_err(|_| "contextual exact context spelling is not UTF-8".to_string())?;
        offset += spelling_len;
    }

    if seen.iter().any(|slot_seen| !*slot_seen) {
        return Err(
            "contextual exact context does not cover every expression variable slot".to_string(),
        );
    }
    if offset != context.len() {
        return Err("contextual exact context contains trailing data".to_string());
    }
    Ok(())
}

fn increment_exact_context_count(
    contexts: &mut HashMap<Vec<u8>, BTreeMap<Vec<u8>, u32>>,
    expr_bytes: &[u8],
    context_bytes: &[u8],
) -> Result<(), String> {
    add_exact_context_count(contexts, expr_bytes, context_bytes, 1)
}

fn add_exact_context_count(
    contexts: &mut HashMap<Vec<u8>, BTreeMap<Vec<u8>, u32>>,
    expr_bytes: &[u8],
    context_bytes: &[u8],
    delta: u32,
) -> Result<(), String> {
    if delta == 0 {
        return Ok(());
    }
    let per_expr = contexts.entry(expr_bytes.to_vec()).or_default();
    let count = per_expr.entry(context_bytes.to_vec()).or_insert(0);
    *count = count
        .checked_add(delta)
        .ok_or_else(|| "contextual exact context multiplicity overflow".to_string())?;
    Ok(())
}

fn ensure_exact_context_count_can_add(
    contexts: &HashMap<Vec<u8>, BTreeMap<Vec<u8>, u32>>,
    expr_bytes: &[u8],
    context_bytes: &[u8],
    delta: u32,
) -> Result<(), String> {
    if delta == 0 {
        return Ok(());
    }
    let current = contexts
        .get(expr_bytes)
        .and_then(|per_expr| per_expr.get(context_bytes))
        .copied()
        .unwrap_or(0);
    current
        .checked_add(delta)
        .map(|_| ())
        .ok_or_else(|| "contextual exact context multiplicity overflow".to_string())
}

fn decrement_any_exact_context_count(
    contexts: &mut HashMap<Vec<u8>, BTreeMap<Vec<u8>, u32>>,
    expr_bytes: &[u8],
) {
    let Some(per_expr) = contexts.get_mut(expr_bytes) else {
        return;
    };
    let Some(first_key) = per_expr.keys().next().cloned() else {
        contexts.remove(expr_bytes);
        return;
    };
    if let Some(count) = per_expr.get_mut(&first_key) {
        if *count > 1 {
            *count -= 1;
        } else {
            per_expr.remove(&first_key);
        }
    }
    if per_expr.is_empty() {
        contexts.remove(expr_bytes);
    }
}

fn has_exact_context_count(
    contexts: &HashMap<Vec<u8>, BTreeMap<Vec<u8>, u32>>,
    expr_bytes: &[u8],
    context_bytes: &[u8],
) -> bool {
    contexts
        .get(expr_bytes)
        .and_then(|per_expr| per_expr.get(context_bytes))
        .copied()
        .unwrap_or(0)
        != 0
}

fn decrement_exact_context_count(
    contexts: &mut HashMap<Vec<u8>, BTreeMap<Vec<u8>, u32>>,
    expr_bytes: &[u8],
    context_bytes: &[u8],
) -> bool {
    let Some(per_expr) = contexts.get_mut(expr_bytes) else {
        return false;
    };
    let Some(count) = per_expr.get_mut(context_bytes) else {
        return false;
    };
    if *count > 1 {
        *count -= 1;
    } else {
        per_expr.remove(context_bytes);
    }
    if per_expr.is_empty() {
        contexts.remove(expr_bytes);
    }
    true
}

fn append_opening_context(out: &mut Vec<u8>, context_id: u32, context_bytes: &[u8]) {
    append_u32_be(out, context_id);
    out.extend_from_slice(context_bytes);
}

fn append_contextual_exact_rows_header(out: &mut Vec<u8>, row_count: u64, context_count: u32) {
    append_u32_be(out, QUERY_ONLY_V2_MAGIC);
    append_u16_be(out, CONTEXTUAL_ROWS_WIRE_VERSION);
    append_u16_be(out, CONTEXTUAL_EXACT_ROWS_FLAGS);
    append_u64_be(out, row_count);
    append_u32_be(out, context_count);
}

fn append_contextual_exact_row(
    out: &mut Vec<u8>,
    context_id: u32,
    multiplicity: u32,
    expr_packet: &[u8],
) -> Result<(), String> {
    if multiplicity == 0 {
        return Err("contextual exact rows require positive multiplicity".to_string());
    }
    let expr_len = u32::try_from(expr_packet.len())
        .map_err(|_| "contextual exact-row expr packet exceeds u32 length".to_string())?;
    append_u32_be(out, context_id);
    append_u32_be(out, multiplicity);
    append_u32_be(out, expr_len);
    out.extend_from_slice(expr_packet);
    Ok(())
}

#[derive(Debug, Clone)]
struct ContextualQueryBinding {
    query_slot: u16,
    value_flags: u32,
    expr_packet: Vec<u8>,
    context: Vec<u8>,
}

#[derive(Debug, Clone)]
struct ContextualQueryCandidate {
    atom_expr_bytes: Vec<u8>,
    count: u32,
    exact_context: Option<Vec<u8>>,
}

#[derive(Debug, Clone)]
struct ContextualResidualRow {
    multiplicity: u64,
    bindings: Vec<ContextualQueryBinding>,
}

#[derive(Debug, Clone, Copy)]
struct ContextualResidualFactorEnv {
    namespace: u8,
    variable_offset: u8,
    byte_offset: u32,
}

struct ContextualResidualQueryCursor {
    pattern_expr_bytes: Vec<u8>,
    factor_envs: Vec<ContextualResidualFactorEnv>,
    candidate_lists: Vec<Vec<ContextualQueryCandidate>>,
    candidate_residual: Vec<Vec<bool>>,
    indices: Vec<usize>,
    occurrence_indices: Vec<u32>,
    opening_groups: Vec<u32>,
    next_opening_group: u32,
    rows_available: bool,
    exhausted: bool,
    rows_emitted: u64,
    rows_aggregated: u64,
}

#[cfg(feature = "pathmap-space")]
fn append_contextual_query_rows_header(out: &mut Vec<u8>, row_count: u64, context_count: u32) {
    append_u32_be(out, QUERY_ONLY_V2_MAGIC);
    append_u16_be(out, CONTEXTUAL_ROWS_WIRE_VERSION);
    append_u16_be(out, CONTEXTUAL_QUERY_ROWS_FLAGS);
    append_u64_be(out, row_count);
    append_u32_be(out, context_count);
}

fn append_contextual_indexed_query_rows_header(
    out: &mut Vec<u8>,
    row_count: u64,
    context_count: u32,
) {
    append_u32_be(out, QUERY_ONLY_V2_MAGIC);
    append_u16_be(out, CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION);
    append_u16_be(out, CONTEXTUAL_INDEXED_QUERY_ROWS_FLAGS);
    append_u64_be(out, row_count);
    append_u32_be(out, context_count);
}

#[cfg(feature = "pathmap-space")]
fn append_query_slot_context_entry(out: &mut Vec<u8>, value_slot: u16, query_slot: u16) {
    append_u16_be(out, value_slot);
    out.push(OPEN_VAR_REF_QUERY_SLOT);
    out.push(0);
    append_u16_be(out, query_slot);
}

#[cfg(feature = "pathmap-space")]
fn append_matched_exact_context_entry_remapped(
    out: &mut Vec<u8>,
    target_slot: u16,
    source_env: u8,
    exact_context: &[u8],
    source_slot: u16,
) -> Result<(), String> {
    if source_env == 0 {
        return Err("matched exact context requires a non-query source environment".to_string());
    }
    let mut offset = 0usize;
    let entry_count = read_u32_be_at(exact_context, &mut offset)?;
    for _ in 0..entry_count {
        let slot = read_u16_be_at(exact_context, &mut offset)?;
        let kind = read_u8_at(exact_context, &mut offset)?;
        let reserved = read_u8_at(exact_context, &mut offset)?;
        if reserved != 0 {
            return Err(
                "contextual query value context entry has nonzero reserved byte".to_string(),
            );
        }
        if kind != OPEN_VAR_REF_EXACT {
            return Err("stored exact context contains non-exact ref kind".to_string());
        }
        if exact_context.len().saturating_sub(offset) < 8 {
            return Err("stored exact context truncated while reading VarId".to_string());
        }
        let var_id_bytes = &exact_context[offset..offset + 8];
        offset += 8;
        let spelling_len = read_u32_be_at(exact_context, &mut offset)? as usize;
        if exact_context.len().saturating_sub(offset) < spelling_len {
            return Err("stored exact context truncated while reading spelling".to_string());
        }
        let spelling = &exact_context[offset..offset + spelling_len];
        offset += spelling_len;
        if slot == source_slot {
            append_u16_be(out, target_slot);
            out.push(OPEN_VAR_REF_MATCHED_EXACT);
            out.push(0);
            out.push(source_env);
            out.push(0);
            out.extend_from_slice(var_id_bytes);
            append_u32_be(out, spelling_len as u32);
            out.extend_from_slice(spelling);
            return Ok(());
        }
    }
    if offset != exact_context.len() {
        return Err("stored exact context contains trailing data".to_string());
    }
    Err(format!(
        "stored exact context does not cover matched value slot {source_slot}"
    ))
}

#[cfg(feature = "pathmap-space")]
fn append_matched_instance_context_entry_remapped(
    out: &mut Vec<u8>,
    target_slot: u16,
    opening_group: u32,
    exact_context: &[u8],
    source_slot: u16,
) -> Result<(), String> {
    if opening_group == 0 {
        return Err("matched opening instance requires a nonzero group".to_string());
    }
    let mut offset = 0usize;
    let entry_count = read_u32_be_at(exact_context, &mut offset)?;
    for _ in 0..entry_count {
        let slot = read_u16_be_at(exact_context, &mut offset)?;
        let kind = read_u8_at(exact_context, &mut offset)?;
        let reserved = read_u8_at(exact_context, &mut offset)?;
        if reserved != 0 {
            return Err(
                "contextual query value context entry has nonzero reserved byte".to_string(),
            );
        }
        if kind != OPEN_VAR_REF_EXACT {
            return Err("stored exact context contains non-exact ref kind".to_string());
        }
        if exact_context.len().saturating_sub(offset) < 8 {
            return Err("stored exact context truncated while reading VarId".to_string());
        }
        let var_id_bytes = &exact_context[offset..offset + 8];
        offset += 8;
        let spelling_len = read_u32_be_at(exact_context, &mut offset)? as usize;
        if exact_context.len().saturating_sub(offset) < spelling_len {
            return Err("stored exact context truncated while reading spelling".to_string());
        }
        let spelling = &exact_context[offset..offset + spelling_len];
        offset += spelling_len;
        if slot == source_slot {
            append_u16_be(out, target_slot);
            out.push(OPEN_VAR_REF_MATCHED_INSTANCE);
            out.push(0);
            append_u32_be(out, opening_group);
            append_u16_be(out, source_slot);
            out.extend_from_slice(var_id_bytes);
            append_u32_be(out, spelling_len as u32);
            out.extend_from_slice(spelling);
            return Ok(());
        }
    }
    if offset != exact_context.len() {
        return Err("stored exact context contains trailing data".to_string());
    }
    Err(format!(
        "stored exact context does not cover matched value slot {source_slot}"
    ))
}

#[cfg(feature = "pathmap-space")]
fn contextual_query_candidates_for_factor(
    space: &BridgeSpace,
    factor_expr: Expr,
) -> Result<Vec<ContextualQueryCandidate>, String> {
    let structural = counted_factor_candidates(&space.inner, factor_expr)?;
    let mut out = Vec::new();

    for entry in structural {
        let expr = Expr {
            ptr: entry.atom_expr_bytes.as_ptr().cast_mut(),
        };
        let encoded = stable_bridge_expr_packet_bytes(&space.inner, expr)?;
        let var_count = bridge_expr_packet_var_count(&encoded)?;

        if let Some(per_expr) = space.exact_contexts.get(&entry.atom_expr_bytes) {
            let mut covered = 0u32;
            for (context, count) in per_expr {
                if *count == 0 {
                    return Err("contextual query exact context has zero multiplicity".to_string());
                }
                validate_contextual_exact_context(context, var_count)?;
                covered = covered.checked_add(*count).ok_or_else(|| {
                    "contextual query exact context multiplicity overflow".to_string()
                })?;
                out.push(ContextualQueryCandidate {
                    atom_expr_bytes: entry.atom_expr_bytes.clone(),
                    count: *count,
                    exact_context: Some(context.clone()),
                });
            }
            if covered > entry.count {
                return Err(
                    "contextual query exact context multiplicity exceeds structural multiplicity"
                        .to_string(),
                );
            }
            if covered < entry.count {
                if var_count != 0 {
                    return Err(
                        "contextual query rows need exact context for matched variable value"
                            .to_string(),
                    );
                }
                out.push(ContextualQueryCandidate {
                    atom_expr_bytes: entry.atom_expr_bytes,
                    count: entry.count - covered,
                    exact_context: None,
                });
            }
        } else if var_count != 0 {
            return Err(
                "contextual query rows need exact context for matched variable value".to_string(),
            );
        } else {
            out.push(ContextualQueryCandidate {
                atom_expr_bytes: entry.atom_expr_bytes,
                count: entry.count,
                exact_context: None,
            });
        }
    }

    Ok(out)
}

#[cfg(feature = "pathmap-space")]
fn contextual_query_candidates_from_counted_entries(
    space: &BridgeSpace,
    entries: Vec<CountedEntry>,
) -> Result<Vec<ContextualQueryCandidate>, String> {
    let mut out = Vec::new();
    for entry in entries {
        let expr = Expr {
            ptr: entry.atom_expr_bytes.as_ptr().cast_mut(),
        };
        let encoded = stable_bridge_expr_packet_bytes(&space.inner, expr)?;
        let var_count = bridge_expr_packet_var_count(&encoded)?;
        if let Some(per_expr) = space.exact_contexts.get(&entry.atom_expr_bytes) {
            let mut covered = 0u32;
            for (context, count) in per_expr {
                if *count == 0 {
                    return Err("indexed contextual candidate has zero multiplicity".to_string());
                }
                validate_contextual_exact_context(context, var_count)?;
                covered = covered.checked_add(*count).ok_or_else(|| {
                    "indexed contextual candidate multiplicity overflow".to_string()
                })?;
                out.push(ContextualQueryCandidate {
                    atom_expr_bytes: entry.atom_expr_bytes.clone(),
                    count: *count,
                    exact_context: Some(context.clone()),
                });
            }
            if covered > entry.count {
                return Err(
                    "indexed contextual candidate contexts exceed structural multiplicity"
                        .to_string(),
                );
            }
            if covered < entry.count {
                out.push(ContextualQueryCandidate {
                    atom_expr_bytes: entry.atom_expr_bytes,
                    count: entry.count - covered,
                    exact_context: None,
                });
            }
        } else {
            out.push(ContextualQueryCandidate {
                atom_expr_bytes: entry.atom_expr_bytes,
                count: entry.count,
                exact_context: None,
            });
        }
    }
    Ok(out)
}

#[cfg(feature = "pathmap-space")]
fn contextual_value_depends_on_namespace(
    expr_env: ExprEnv,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
    namespace: u8,
    resolving: &mut Vec<(u8, u8)>,
) -> bool {
    if let Some(var) = expr_env.var_opt() {
        if let Some(rhs) = bindings.get(&var) {
            if resolving.contains(&var) {
                return true;
            }
            resolving.push(var);
            let depends =
                contextual_value_depends_on_namespace(*rhs, bindings, namespace, resolving);
            resolving.pop();
            return depends;
        }
        return var.0 == namespace;
    }
    if expr_env.subsexpr().arity().is_none() {
        return false;
    }
    let mut args = Vec::new();
    expr_env.args(&mut args);
    args.into_iter()
        .any(|arg| contextual_value_depends_on_namespace(arg, bindings, namespace, resolving))
}

#[cfg(feature = "pathmap-space")]
fn contextless_candidate_can_escape(
    factor: ContextualResidualFactorEnv,
    candidate: &ContextualQueryCandidate,
    factor_idx: usize,
    pattern_expr: Expr,
) -> bool {
    let atom_expr = Expr {
        ptr: candidate.atom_expr_bytes.as_ptr().cast_mut(),
    };
    if atom_expr.is_ground() || candidate.exact_context.is_some() {
        return false;
    }
    let candidate_namespace = (factor_idx + 1) as u8;
    let mut stack = vec![(
        ExprEnv {
            n: factor.namespace,
            v: factor.variable_offset,
            offset: factor.byte_offset,
            base: pattern_expr,
        },
        ExprEnv::new(candidate_namespace, atom_expr),
    )];
    let Ok(bindings) = unify(&mut stack) else {
        return false;
    };
    bindings.iter().any(|(&(side, _), value)| {
        side == 0
            && contextual_value_depends_on_namespace(
                *value,
                &bindings,
                candidate_namespace,
                &mut Vec::new(),
            )
    })
}

#[cfg(feature = "pathmap-space")]
impl ContextualResidualQueryCursor {
    fn new(
        pattern_expr_bytes: &[u8],
        candidate_lists: Vec<Vec<ContextualQueryCandidate>>,
    ) -> Result<Self, String> {
        let owned_pattern = pattern_expr_bytes.to_vec();
        let pattern_expr = Expr {
            ptr: owned_pattern.as_ptr().cast_mut(),
        };
        let factor_count = pattern_expr
            .arity()
            .ok_or_else(|| "indexed contextual cursor expected a wrapped query".to_string())?
            .checked_sub(1)
            .ok_or_else(|| "indexed contextual cursor expected a wrapped query".to_string())?;
        if factor_count == 0 || factor_count as usize != candidate_lists.len() {
            return Err("indexed contextual cursor factor-source count mismatch".to_string());
        }
        if factor_count == u8::MAX {
            return Err("indexed contextual cursor exceeded factor namespace capacity".to_string());
        }
        let mut pat_args = Vec::with_capacity((factor_count as usize) + 1);
        ExprEnv::new(0, pattern_expr).args(&mut pat_args);
        let factor_envs = pat_args[1..]
            .iter()
            .map(|factor| ContextualResidualFactorEnv {
                namespace: factor.n,
                variable_offset: factor.v,
                byte_offset: factor.offset,
            })
            .collect::<Vec<_>>();
        let rows_available = !factor_envs.iter().zip(&candidate_lists).enumerate().any(
            |(factor_idx, (factor, candidates))| {
                candidates.iter().any(|candidate| {
                    contextless_candidate_can_escape(*factor, candidate, factor_idx, pattern_expr)
                })
            },
        );
        let candidate_residual = candidate_lists
            .iter()
            .map(|candidates| {
                candidates
                    .iter()
                    .map(|entry| {
                        !Expr {
                            ptr: entry.atom_expr_bytes.as_ptr().cast_mut(),
                        }
                        .is_ground()
                    })
                    .collect::<Vec<_>>()
            })
            .collect::<Vec<_>>();
        let exhausted = candidate_lists.iter().any(Vec::is_empty);
        let opening_groups = (1..=factor_envs.len())
            .map(|group| u32::try_from(group).expect("factor count fits u32"))
            .collect::<Vec<_>>();
        let next_opening_group = u32::try_from(factor_envs.len())
            .map_err(|_| "indexed contextual cursor factor count exceeded u32".to_string())?
            .checked_add(1)
            .ok_or_else(|| "indexed contextual opening group overflow".to_string())?;
        Ok(Self {
            indices: vec![0; factor_envs.len()],
            occurrence_indices: vec![0; factor_envs.len()],
            pattern_expr_bytes: owned_pattern,
            factor_envs,
            candidate_lists,
            candidate_residual,
            opening_groups,
            next_opening_group,
            rows_available,
            exhausted,
            rows_emitted: 0,
            rows_aggregated: 0,
        })
    }

    fn rows_emitted(&self) -> u64 {
        self.rows_emitted
    }

    fn rows_aggregated(&self) -> u64 {
        self.rows_aggregated
    }

    fn rows_available(&self) -> bool {
        self.rows_available
    }

    fn chosen_contains_residual(&self) -> bool {
        self.indices.iter().enumerate().any(|(factor_idx, index)| {
            self.candidate_residual
                .get(factor_idx)
                .and_then(|flags| flags.get(*index))
                .copied()
                .unwrap_or(false)
        })
    }

    fn chosen_row_multiplicity(&self) -> Result<u64, String> {
        let mut multiplicity = 1u64;
        for factor_idx in 0..self.indices.len() {
            let candidate_idx = self.indices[factor_idx];
            let chosen = &self.candidate_lists[factor_idx][candidate_idx];
            if !self.candidate_residual[factor_idx][candidate_idx] {
                multiplicity = multiplicity
                    .checked_mul(u64::from(chosen.count))
                    .ok_or_else(|| {
                        "indexed contextual row multiplicity overflowed u64".to_string()
                    })?;
            }
        }
        Ok(multiplicity)
    }

    fn remaining_opening_activations(&self) -> Result<u64, String> {
        let mut total = 1u64;
        let mut position = 0u64;
        for factor_idx in 0..self.indices.len() {
            let candidate_idx = self.indices[factor_idx];
            let radix = if self.candidate_residual[factor_idx][candidate_idx] {
                u64::from(self.candidate_lists[factor_idx][candidate_idx].count)
            } else {
                1u64
            };
            let occurrence = u64::from(self.occurrence_indices[factor_idx]);
            if radix == 0 || occurrence >= radix {
                return Err("indexed contextual occurrence state is invalid".to_string());
            }
            total = total
                .checked_mul(radix)
                .ok_or_else(|| "indexed contextual activation count overflowed u64".to_string())?;
            position = position
                .checked_mul(radix)
                .and_then(|prefix| prefix.checked_add(occurrence))
                .ok_or_else(|| {
                    "indexed contextual activation position overflowed u64".to_string()
                })?;
        }
        total
            .checked_sub(position)
            .ok_or_else(|| "indexed contextual activation position exceeded count".to_string())
    }

    fn assign_opening_groups_from(&mut self, first_factor: usize) -> Result<(), String> {
        for group in self.opening_groups.iter_mut().skip(first_factor) {
            *group = self.next_opening_group;
            self.next_opening_group = self
                .next_opening_group
                .checked_add(1)
                .ok_or_else(|| "indexed contextual opening group overflow".to_string())?;
        }
        Ok(())
    }

    fn advance(&mut self) -> Result<(), String> {
        for factor_idx in (0..self.indices.len()).rev() {
            let candidate_idx = self.indices[factor_idx];
            let occurrence_count = if self.candidate_residual[factor_idx][candidate_idx] {
                self.candidate_lists[factor_idx][candidate_idx].count
            } else {
                1u32
            };
            self.occurrence_indices[factor_idx] += 1;
            if self.occurrence_indices[factor_idx] < occurrence_count {
                self.assign_opening_groups_from(factor_idx)?;
                return Ok(());
            }
            self.occurrence_indices[factor_idx] = 0;
            self.indices[factor_idx] += 1;
            if self.indices[factor_idx] < self.candidate_lists[factor_idx].len() {
                self.assign_opening_groups_from(factor_idx)?;
                return Ok(());
            }
            self.indices[factor_idx] = 0;
        }
        self.exhausted = true;
        Ok(())
    }

    fn advance_candidate_combination(&mut self) {
        self.occurrence_indices.fill(0);
        for factor_idx in (0..self.indices.len()).rev() {
            self.indices[factor_idx] += 1;
            if self.indices[factor_idx] < self.candidate_lists[factor_idx].len() {
                return;
            }
            self.indices[factor_idx] = 0;
        }
        self.exhausted = true;
    }

    fn next_row(&mut self, space: &Space) -> Result<Option<ContextualResidualRow>, String> {
        if !self.rows_available {
            return Err("indexed cursor supports aggregation but not row emission".to_string());
        }
        while !self.exhausted {
            if !self.chosen_contains_residual() {
                self.advance()?;
                continue;
            }
            let pattern_expr = Expr {
                ptr: self.pattern_expr_bytes.as_ptr().cast_mut(),
            };
            let mut stack = Vec::with_capacity(self.factor_envs.len());
            let mut chosen_entries = Vec::with_capacity(self.factor_envs.len());
            let multiplicity = self.chosen_row_multiplicity()?;
            for factor_idx in 0..self.factor_envs.len() {
                let factor = self.factor_envs[factor_idx];
                let factor_expr_env = ExprEnv {
                    n: factor.namespace,
                    v: factor.variable_offset,
                    offset: factor.byte_offset,
                    base: pattern_expr,
                };
                let chosen = &self.candidate_lists[factor_idx][self.indices[factor_idx]];
                let atom_expr = Expr {
                    ptr: chosen.atom_expr_bytes.as_ptr().cast_mut(),
                };
                stack.push((
                    factor_expr_env,
                    ExprEnv::new((factor_idx + 1) as u8, atom_expr),
                ));
                chosen_entries.push(chosen);
            }
            let row = if let Ok(bindings) = unify(&mut stack) {
                let mut encoded = Vec::new();
                for (&(side, idx), expr_env) in bindings.iter() {
                    if side == 0 {
                        encoded.push(encode_contextual_query_binding(
                            space,
                            idx,
                            *expr_env,
                            &bindings,
                            &chosen_entries,
                            Some(&self.opening_groups),
                        )?);
                    }
                }
                Some(ContextualResidualRow {
                    multiplicity,
                    bindings: encoded,
                })
            } else {
                None
            };
            self.advance()?;
            if row.is_some() {
                self.rows_emitted = self.rows_emitted.saturating_add(1);
                return Ok(row);
            }
        }
        Ok(None)
    }

    fn count_remaining(&mut self) -> Result<u64, String> {
        let mut total = 0u64;
        while !self.exhausted {
            if !self.chosen_contains_residual() {
                self.advance_candidate_combination();
                continue;
            }
            let pattern_expr = Expr {
                ptr: self.pattern_expr_bytes.as_ptr().cast_mut(),
            };
            let mut stack = Vec::with_capacity(self.factor_envs.len());
            let multiplicity = self.chosen_row_multiplicity()?;
            let remaining_activations = self.remaining_opening_activations()?;
            for factor_idx in 0..self.factor_envs.len() {
                let factor = self.factor_envs[factor_idx];
                let chosen = &self.candidate_lists[factor_idx][self.indices[factor_idx]];
                stack.push((
                    ExprEnv {
                        n: factor.namespace,
                        v: factor.variable_offset,
                        offset: factor.byte_offset,
                        base: pattern_expr,
                    },
                    ExprEnv::new(
                        (factor_idx + 1) as u8,
                        Expr {
                            ptr: chosen.atom_expr_bytes.as_ptr().cast_mut(),
                        },
                    ),
                ));
            }
            if unify(&mut stack).is_ok() {
                let remaining =
                    multiplicity
                        .checked_mul(remaining_activations)
                        .ok_or_else(|| {
                            "indexed contextual count multiplicity overflowed u64".to_string()
                        })?;
                total = total.checked_add(remaining).ok_or_else(|| {
                    "indexed contextual count exceeds u64 aggregate capacity".to_string()
                })?;
                self.rows_aggregated = self.rows_aggregated.saturating_add(1);
            }
            self.advance_candidate_combination();
        }
        Ok(total)
    }
}

#[cfg(feature = "pathmap-space")]
fn build_contextual_query_value_context(
    origins: &BTreeMap<u8, (u8, u8)>,
    chosen_entries: &[&ContextualQueryCandidate],
    opening_groups: Option<&[u32]>,
) -> Result<Vec<u8>, String> {
    let mut context = Vec::new();
    let entry_count = u32::try_from(origins.len())
        .map_err(|_| "contextual query value context exceeded u32 entry count".to_string())?;
    append_u32_be(&mut context, entry_count);
    for (&value_slot, &(source_env, source_slot)) in origins.iter() {
        if source_env == 0 {
            append_query_slot_context_entry(
                &mut context,
                u16::from(value_slot),
                u16::from(source_slot),
            );
            continue;
        }
        let factor_idx = usize::from(source_env - 1);
        let Some(entry) = chosen_entries.get(factor_idx) else {
            return Err(format!(
                "contextual query rows reference missing matched factor env {source_env}"
            ));
        };
        let exact_context = entry.exact_context.as_deref().ok_or_else(|| {
            "contextual query rows need exact context for matched variable value".to_string()
        })?;
        if let Some(groups) = opening_groups {
            let opening_group = *groups.get(factor_idx).ok_or_else(|| {
                format!("contextual query rows lack opening group for factor {factor_idx}")
            })?;
            append_matched_instance_context_entry_remapped(
                &mut context,
                u16::from(value_slot),
                opening_group,
                exact_context,
                u16::from(source_slot),
            )?;
        } else {
            append_matched_exact_context_entry_remapped(
                &mut context,
                u16::from(value_slot),
                source_env,
                exact_context,
                u16::from(source_slot),
            )?;
        }
    }
    Ok(context)
}

#[cfg(feature = "pathmap-space")]
fn append_contextual_query_binding(
    out: &mut Vec<u8>,
    binding: &ContextualQueryBinding,
    context_id: u32,
) -> Result<(), String> {
    let expr_len = u32::try_from(binding.expr_packet.len())
        .map_err(|_| "contextual query binding expr packet exceeds u32 length".to_string())?;
    append_u16_be(out, binding.query_slot);
    append_u32_be(out, context_id);
    append_u32_be(out, binding.value_flags);
    append_u32_be(out, expr_len);
    out.extend_from_slice(&binding.expr_packet);
    Ok(())
}

fn append_query_only_v2_header(out: &mut Vec<u8>, row_count: u64) {
    append_u32_be(out, QUERY_ONLY_V2_MAGIC);
    append_u16_be(out, QUERY_ONLY_V2_VERSION);
    append_u16_be(
        out,
        QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY
            | QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES
            | QUERY_ONLY_V2_FLAG_WIDE_TOKENS,
    );
    append_u64_be(out, row_count);
}

#[cfg(feature = "pathmap-space")]
fn append_multi_ref_v3_header(out: &mut Vec<u8>, flags: u16, factor_count: u32, row_count: u64) {
    append_u32_be(out, QUERY_ONLY_V2_MAGIC);
    append_u16_be(out, MULTI_REF_V3_VERSION);
    append_u16_be(out, flags);
    append_u32_be(out, factor_count);
    append_u64_be(out, row_count);
}

fn append_bindings_packet(
    space: &Space,
    out: &mut Vec<u8>,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
    atom_indices: &[u32],
) -> Result<(), String> {
    append_u32_be(out, atom_indices.len() as u32);
    for &idx in atom_indices {
        append_u32_be(out, idx);
    }
    append_u32_be(out, bindings.len() as u32);
    for (&(key_a, key_b), expr_env) in bindings.iter() {
        out.push(key_a);
        out.push(key_b);
        let rendered = bridge_expr_env_text(space, *expr_env)?;
        append_u32_be(out, rendered.len() as u32);
        out.extend_from_slice(&rendered);
    }
    Ok(())
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

fn append_query_only_v2_row(
    space: &Space,
    out: &mut Vec<u8>,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
) -> Result<(), String> {
    append_u32_be(out, 0);
    append_query_only_binding_entries(space, out, bindings)
}

fn append_empty_binding_row(out: &mut Vec<u8>) {
    append_u32_be(out, 0);
    append_u32_be(out, 0);
}

#[cfg(feature = "pathmap-space")]
fn encode_contextual_query_value_rec(
    space: &Space,
    expr_env: ExprEnv,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
    row_vars: &mut BTreeMap<(u8, u8), u8>,
    origins: &mut BTreeMap<u8, (u8, u8)>,
    next_row_var: &mut u8,
    resolving: &mut Vec<(u8, u8)>,
    out: &mut Vec<u8>,
) -> Result<bool, String> {
    if let Some(var) = expr_env.var_opt() {
        if let Some(rhs) = bindings.get(&var) {
            if resolving.contains(&var) {
                return Err(
                    "contextual query packet materialization hit a recursive cycle".to_string(),
                );
            }
            resolving.push(var);
            let is_ground = encode_contextual_query_value_rec(
                space,
                *rhs,
                bindings,
                row_vars,
                origins,
                next_row_var,
                resolving,
                out,
            )?;
            resolving.pop();
            return Ok(is_ground);
        }

        if let Some(existing) = row_vars.get(&var) {
            out.push(BRIDGE_EXPR_TAG_VARREF);
            out.push(*existing);
            return Ok(false);
        }

        let slot = *next_row_var;
        *next_row_var = next_row_var
            .checked_add(1)
            .ok_or_else(|| "contextual query packet row exhausted u8 variable slots".to_string())?;
        row_vars.insert(var, slot);
        origins.insert(slot, var);
        out.push(BRIDGE_EXPR_TAG_NEWVAR);
        return Ok(false);
    }

    let expr = expr_env.subsexpr();
    if let Some(arity) = expr.arity() {
        out.push(BRIDGE_EXPR_TAG_ARITY);
        append_u32_be(out, arity as u32);
        let mut args = Vec::new();
        expr_env.args(&mut args);
        let mut is_ground = true;
        for arg in args {
            if !encode_contextual_query_value_rec(
                space,
                arg,
                bindings,
                row_vars,
                origins,
                next_row_var,
                resolving,
                out,
            )? {
                is_ground = false;
            }
        }
        return Ok(is_ground);
    }

    out.extend_from_slice(&stable_bridge_expr_packet_bytes(space, expr)?);
    Ok(true)
}

#[cfg(feature = "pathmap-space")]
fn encode_contextual_query_binding(
    space: &Space,
    query_slot: u8,
    expr_env: ExprEnv,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
    chosen_entries: &[&ContextualQueryCandidate],
    opening_groups: Option<&[u32]>,
) -> Result<ContextualQueryBinding, String> {
    let mut row_vars = BTreeMap::<(u8, u8), u8>::new();
    let mut origins = BTreeMap::<u8, (u8, u8)>::new();
    let mut next_row_var = 0u8;
    let mut resolving = Vec::new();
    let mut expr_packet = Vec::new();
    encode_contextual_query_value_rec(
        space,
        expr_env,
        bindings,
        &mut row_vars,
        &mut origins,
        &mut next_row_var,
        &mut resolving,
        &mut expr_packet,
    )?;
    let var_count = bridge_expr_packet_var_count(&expr_packet)?;
    if usize::from(var_count) != origins.len() {
        return Err("contextual query value context does not cover every value slot".to_string());
    }
    let context = build_contextual_query_value_context(&origins, chosen_entries, opening_groups)?;
    Ok(ContextualQueryBinding {
        query_slot: u16::from(query_slot),
        value_flags: 0,
        expr_packet,
        context,
    })
}

#[cfg(feature = "pathmap-space")]
fn append_contextual_query_packet_row(
    out: &mut Vec<u8>,
    row: &[ContextualQueryBinding],
    context_ids: &mut BTreeMap<Vec<u8>, u32>,
    contexts: &mut Vec<Vec<u8>>,
) -> Result<(), String> {
    let binding_count = u32::try_from(row.len())
        .map_err(|_| "contextual query row exceeded u32 binding count".to_string())?;
    append_u32_be(out, binding_count);
    for binding in row {
        let context_id = if let Some(id) = context_ids.get(&binding.context) {
            *id
        } else {
            let id = u32::try_from(contexts.len())
                .map_err(|_| "contextual query packet exceeded u32 context count".to_string())?;
            context_ids.insert(binding.context.clone(), id);
            contexts.push(binding.context.clone());
            id
        };
        append_contextual_query_binding(out, binding, context_id)?;
    }
    Ok(())
}

#[cfg(feature = "pathmap-space")]
fn append_contextual_indexed_query_packet_row(
    out: &mut Vec<u8>,
    row: &ContextualResidualRow,
    context_ids: &mut BTreeMap<Vec<u8>, u32>,
    contexts: &mut Vec<Vec<u8>>,
) -> Result<(), String> {
    if row.multiplicity == 0 {
        return Err("indexed contextual row requires positive multiplicity".to_string());
    }
    append_u64_be(out, row.multiplicity);
    append_contextual_query_packet_row(out, &row.bindings, context_ids, contexts)
}

#[cfg(feature = "pathmap-space")]
fn rollback_contextual_packet_contexts(
    context_ids: &mut BTreeMap<Vec<u8>, u32>,
    contexts: &mut Vec<Vec<u8>>,
    mark: usize,
) {
    while contexts.len() > mark {
        let context = contexts
            .pop()
            .expect("context length was checked before rollback");
        let removed = context_ids.remove(&context);
        debug_assert!(removed.is_some());
    }
}

fn query_bindings_packet(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(Vec<u8>, u64), String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(&mut space.inner, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    let mut rows: Vec<Vec<u8>> = Vec::new();
    let mut error: Option<String> = None;

    Space::query_multi(&space.inner.btm, pattern_expr, |result, _matched_expr| {
        let mut row = Vec::new();
        let append_result = match result {
            Ok(_refs) => {
                append_empty_binding_row(&mut row);
                Ok(())
            }
            Err(bindings) => append_bindings_packet(&space.inner, &mut row, &bindings, &[]),
        };
        match append_result {
            Ok(()) => {
                rows.push(row);
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

    let row_count = checked_packet_count(rows.len(), "query bindings packet row count")?;
    let mut packet = Vec::new();
    append_u64_be(&mut packet, row_count);
    for row in rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

fn visit_query_bindings_query_only_v2_rows(
    space: &mut BridgeSpace,
    pattern: &[u8],
    mut visit: impl FnMut(&[u8]) -> Result<(), String>,
) -> Result<u64, String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(&mut space.inner, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    ensure_query_only_v2_shape(pattern_expr)?;
    if bridge_uses_counted_storage(space) {
        let rows = counted_query_only_packet_rows(&space.inner, &pattern_bytes)?;
        let row_count = checked_packet_count(rows.len(), "query-only v2 packet row count")?;
        for row in rows {
            visit(&row)?;
        }
        return Ok(row_count);
    }

    let mut error: Option<String> = None;
    let mut row_count = 0u64;
    let mut row = Vec::new();

    Space::query_multi(&space.inner.btm, pattern_expr, |result, _matched_expr| {
        row.clear();
        let append_result = match result {
            Ok(_refs) => {
                append_empty_binding_row(&mut row);
                Ok(())
            }
            Err(bindings) => append_query_only_v2_row(&space.inner, &mut row, &bindings),
        };
        match append_result {
            Ok(()) => {
                row_count = match row_count.checked_add(1) {
                    Some(next) => next,
                    None => {
                        error = Some(
                            "query-only v2 packet row count exceeds u64 packet limit".to_string(),
                        );
                        return false;
                    }
                };
                match visit(&row) {
                    Ok(()) => true,
                    Err(err) => {
                        error = Some(err);
                        false
                    }
                }
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
    Ok(row_count)
}

fn query_bindings_query_only_v2_rows(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(u64, Vec<Vec<u8>>), String> {
    let mut rows = Vec::new();
    let row_count = visit_query_bindings_query_only_v2_rows(space, pattern, |row| {
        rows.push(row.to_vec());
        Ok(())
    })?;
    Ok((row_count, rows))
}

fn query_bindings_query_only_v2_chunked_rows(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(u64, MaterializedQueryRows), String> {
    let mut rows = MaterializedQueryRows::default();
    let row_count = visit_query_bindings_query_only_v2_rows(space, pattern, |row| {
        rows.push(row);
        Ok(())
    })?;
    Ok((row_count, rows))
}

fn query_bindings_query_only_v2_packet(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(Vec<u8>, u64), String> {
    let (row_count, pending_rows) = query_bindings_query_only_v2_rows(space, pattern)?;
    let mut packet = Vec::new();

    append_query_only_v2_header(&mut packet, row_count);
    for row in pending_rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

#[cfg(feature = "pathmap-space")]
fn query_bindings_multi_ref_v3_rows(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(u32, u64, Vec<Vec<u8>>), String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(&mut space.inner, &normalized)?;

    let (factor_count, row_count, pending_rows): (u32, u64, Vec<Vec<u8>>);

    if bridge_uses_counted_storage(space) {
        let detailed_packet_rows =
            counted_query_rows_detailed_packet_rows(&space.inner, &pattern_bytes)?;
        factor_count = detailed_packet_rows.factor_count;
        row_count = checked_packet_count(
            detailed_packet_rows.rows.len(),
            "multi-ref v3 packet row count",
        )?;
        pending_rows = detailed_packet_rows.rows;
    } else {
        return Err(
            "multi-ref v3 packets are only available for counted PathMap bridge spaces".to_string(),
        );
    }
    Ok((factor_count, row_count, pending_rows))
}

#[cfg(feature = "pathmap-space")]
fn query_bindings_multi_ref_v3_packet(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(Vec<u8>, u64), String> {
    let (factor_count, row_count, pending_rows) = query_bindings_multi_ref_v3_rows(space, pattern)?;
    let mut packet = Vec::new();

    append_multi_ref_v3_header(
        &mut packet,
        MULTI_REF_V3_FLAG_QUERY_KEYS_ONLY
            | MULTI_REF_V3_FLAG_RAW_EXPR_BYTES
            | MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES
            | MULTI_REF_V3_FLAG_WIDE_TOKENS,
        factor_count,
        row_count,
    );
    for row in pending_rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

#[cfg(not(feature = "pathmap-space"))]
fn query_bindings_multi_ref_v3_rows(
    _space: &mut BridgeSpace,
    _pattern: &[u8],
) -> Result<(u32, u64, Vec<Vec<u8>>), String> {
    Err("multi-ref v3 packets require the pathmap-space bridge feature".to_string())
}

#[cfg(not(feature = "pathmap-space"))]
fn query_bindings_multi_ref_v3_packet(
    _space: &mut BridgeSpace,
    _pattern: &[u8],
) -> Result<(Vec<u8>, u64), String> {
    Err("multi-ref v3 packets require the pathmap-space bridge feature".to_string())
}

#[cfg(feature = "pathmap-space")]
fn accumulate_contextual_query_rows(
    space: &BridgeSpace,
    factors: &[ExprEnv],
    candidate_lists: &[Vec<ContextualQueryCandidate>],
    depth: usize,
    chosen: &mut Vec<usize>,
    rows: &mut Vec<Vec<ContextualQueryBinding>>,
) -> Result<(), String> {
    if depth == factors.len() {
        let mut stack = Vec::with_capacity(factors.len());
        let mut multiplicity = 1u64;
        let mut chosen_entries = Vec::with_capacity(factors.len());
        for (factor_idx, factor) in factors.iter().enumerate() {
            let chosen_entry = &candidate_lists[factor_idx][chosen[factor_idx]];
            let atom_expr = Expr {
                ptr: chosen_entry.atom_expr_bytes.as_ptr().cast_mut(),
            };
            stack.push((*factor, ExprEnv::new((factor_idx + 1) as u8, atom_expr)));
            multiplicity = multiplicity
                .checked_mul(u64::from(chosen_entry.count))
                .ok_or_else(|| "contextual query row multiplicity overflowed u64".to_string())?;
            chosen_entries.push(chosen_entry);
        }

        if let Ok(bindings) = unify(&mut stack) {
            let mut row = Vec::new();
            for (&(side, idx), expr_env) in bindings.iter() {
                if side != 0 {
                    continue;
                }
                row.push(encode_contextual_query_binding(
                    &space.inner,
                    idx,
                    *expr_env,
                    &bindings,
                    &chosen_entries,
                    None,
                )?);
            }
            let repeat = usize::try_from(multiplicity)
                .map_err(|_| "contextual query row multiplicity exceeds usize".to_string())?;
            for _ in 0..repeat {
                rows.push(row.clone());
            }
        }
        return Ok(());
    }

    for idx in 0..candidate_lists[depth].len() {
        chosen.push(idx);
        accumulate_contextual_query_rows(space, factors, candidate_lists, depth + 1, chosen, rows)?;
        chosen.pop();
    }
    Ok(())
}

#[cfg(feature = "pathmap-space")]
fn query_contextual_rows_packet(
    space: &mut BridgeSpace,
    pattern: &[u8],
) -> Result<(Vec<u8>, u64), String> {
    if !bridge_uses_counted_storage(space) {
        return Err(
            "contextual query rows are only available for counted PathMap bridge spaces"
                .to_string(),
        );
    }

    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(&mut space.inner, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    let factor_count = pattern_expr
        .arity()
        .ok_or_else(|| "contextual query rows expected a wrapped query".to_string())?
        .checked_sub(1)
        .ok_or_else(|| "contextual query rows expected a wrapped query".to_string())?;
    if factor_count == 0 {
        return Err("contextual query rows require at least one query factor".to_string());
    }

    let mut pat_args = Vec::with_capacity((factor_count as usize) + 1);
    ExprEnv::new(0, pattern_expr).args(&mut pat_args);
    let factors = &pat_args[1..];
    let mut candidate_lists = Vec::with_capacity(factors.len());
    for factor in factors {
        let candidates = contextual_query_candidates_for_factor(space, factor.subsexpr())?;
        if candidates.is_empty() {
            let mut packet = Vec::new();
            append_contextual_query_rows_header(&mut packet, 0, 0);
            return Ok((packet, 0));
        }
        candidate_lists.push(candidates);
    }

    let mut rows = Vec::new();
    let mut chosen = Vec::with_capacity(factors.len());
    accumulate_contextual_query_rows(space, factors, &candidate_lists, 0, &mut chosen, &mut rows)?;

    let row_count = checked_packet_count(rows.len(), "contextual query packet row count")?;
    let mut context_ids = BTreeMap::<Vec<u8>, u32>::new();
    let mut contexts = Vec::<Vec<u8>>::new();
    let mut row_bytes = Vec::<Vec<u8>>::new();
    for row in &rows {
        let mut encoded_row = Vec::new();
        append_contextual_query_packet_row(&mut encoded_row, row, &mut context_ids, &mut contexts)?;
        row_bytes.push(encoded_row);
    }
    let context_count = u32::try_from(contexts.len())
        .map_err(|_| "contextual query packet exceeded u32 context count".to_string())?;
    let mut packet = Vec::new();
    append_contextual_query_rows_header(&mut packet, row_count, context_count);
    for (id, context) in contexts.iter().enumerate() {
        let context_id = u32::try_from(id)
            .map_err(|_| "contextual query packet exceeded u32 context id".to_string())?;
        append_opening_context(&mut packet, context_id, context);
    }
    for row in row_bytes {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

#[cfg(not(feature = "pathmap-space"))]
fn query_contextual_rows_packet(
    _space: &mut BridgeSpace,
    _pattern: &[u8],
) -> Result<(Vec<u8>, u64), String> {
    Err("contextual query rows require the pathmap-space bridge feature".to_string())
}

fn query_debug_text(space: &mut Space, pattern: &[u8]) -> Result<(Vec<u8>, u64), String> {
    let normalized = normalize_query_text(pattern)?;
    let pattern_bytes = parse_single_expr(space, &normalized)?;
    let pattern_expr = Expr {
        ptr: pattern_bytes.as_ptr().cast_mut(),
    };
    let mut lines = Vec::new();
    let mut count = 0u64;
    let mut error: Option<String> = None;

    Space::query_multi(&space.btm, pattern_expr, |result, matched_expr| {
        count += 1;
        let mut line = Vec::new();
        let append_result = (|| -> Result<(), String> {
            line.extend_from_slice(b"match=");
            line.extend_from_slice(&bridge_expr_text(space, matched_expr)?);
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
                        line.extend_from_slice(&bridge_expr_env_text(space, *expr_env)?);
                    }
                    line.extend_from_slice(b"]");
                }
            }
            Ok(())
        })();
        if let Err(err) = append_result {
            error = Some(err);
            return false;
        }
        line.push(b'\n');
        lines.extend_from_slice(&line);
        true
    });

    if let Some(err) = error {
        return Err(err);
    }

    Ok((lines, count))
}

fn query_cursor_packet_would_exceed(packet_len: usize, row_len: usize, max_bytes: usize) -> bool {
    packet_len != 0
        && packet_len
            .checked_add(row_len)
            .map(|next_len| next_len > max_bytes)
            .unwrap_or(true)
}

fn query_replay_lookup(
    cache: &Arc<Mutex<QueryReplayCache>>,
    key: &(u64, Vec<u8>),
) -> Option<Arc<Vec<Vec<u8>>>> {
    let mut replay = cache.lock().ok()?;
    let rows = replay.entries.get(key)?.clone();
    replay.hits = replay.hits.saturating_add(1);
    Some(rows)
}

fn query_cursor_capture_row(cursor: &mut BridgeQueryCursor, row: &[u8]) {
    let Some(capture) = cursor.replay_capture.as_mut() else {
        return;
    };
    if !capture.eligible {
        return;
    }
    let Some(next_bytes) = capture.bytes.checked_add(row.len()) else {
        capture.eligible = false;
        capture.rows.clear();
        return;
    };
    if capture.rows.len() >= QUERY_REPLAY_MAX_ROWS || next_bytes > QUERY_REPLAY_MAX_BYTES {
        capture.eligible = false;
        capture.rows.clear();
        return;
    }
    capture.bytes = next_bytes;
    capture.rows.push(row.to_vec());
}

fn query_cursor_publish_completed_replay(cursor: &mut BridgeQueryCursor) {
    let Some(capture) = cursor.replay_capture.take() else {
        return;
    };
    if !capture.eligible {
        return;
    }
    let row_count = capture.rows.len() as u64;
    let Ok(mut replay) = capture.cache.lock() else {
        return;
    };
    if replay.entries.len() >= QUERY_REPLAY_MAX_ENTRIES
        && !replay.entries.contains_key(&capture.key)
    {
        if let Some(oldest_key) = replay.entries.keys().next().cloned() {
            replay.entries.remove(&oldest_key);
        }
    }
    replay.entries.insert(capture.key, Arc::new(capture.rows));
    replay.completions = replay.completions.saturating_add(1);
    replay.rows_stored = replay.rows_stored.saturating_add(row_count);
}

#[cfg(feature = "pathmap-space")]
fn query_cursor_next_indexed_exact_residual_packet(
    cursor: &mut BridgeQueryCursor,
    max_rows: u64,
    max_bytes: usize,
) -> Result<(Vec<u8>, u64), String> {
    let BridgeQueryCursorSource::IndexedExactResidual {
        exact,
        exact_done,
        residual_space,
        residual,
    } = &mut cursor.source
    else {
        return Err("indexed exact-residual packet requested for another cursor kind".to_string());
    };

    if !*exact_done || cursor.pending_row.is_some() {
        let header_len = 20usize;
        if max_bytes <= header_len {
            return Err("query-row batch max_bytes is smaller than the packet header".to_string());
        }
        let mut rows = Vec::<Vec<u8>>::new();
        let mut packet_len = header_len;
        while rows.len() < usize::try_from(max_rows).unwrap_or(usize::MAX) {
            let row = if let Some(row) = cursor.pending_row.take() {
                Some(row)
            } else {
                exact.next_packet_row()?
            };
            let Some(row) = row else {
                *exact_done = true;
                break;
            };
            if query_cursor_packet_would_exceed(packet_len, row.len(), max_bytes) {
                cursor.pending_row = Some(row);
                if rows.is_empty() {
                    return Err("query-row batch row exceeds max_bytes".to_string());
                }
                break;
            }
            packet_len += row.len();
            rows.push(row);
        }
        if !rows.is_empty() {
            let row_count = checked_packet_count(rows.len(), "indexed exact cursor batch rows")?;
            let factor_count = match cursor.kind {
                BridgeQueryCursorKind::MultiRefV3 { factor_count } => factor_count,
                BridgeQueryCursorKind::QueryOnlyV2 => {
                    return Err("indexed exact-residual cursor lost its factor count".to_string());
                }
            };
            let mut packet = Vec::with_capacity(packet_len);
            append_multi_ref_v3_header(
                &mut packet,
                MULTI_REF_V3_FLAG_QUERY_KEYS_ONLY
                    | MULTI_REF_V3_FLAG_RAW_EXPR_BYTES
                    | MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES
                    | MULTI_REF_V3_FLAG_WIDE_TOKENS,
                factor_count,
                row_count,
            );
            for row in rows {
                packet.extend_from_slice(&row);
            }
            return Ok((packet, row_count));
        }
    }

    let header_len = 20usize;
    if max_bytes <= header_len {
        return Err("query-row batch max_bytes is smaller than the packet header".to_string());
    }
    let mut context_ids = BTreeMap::<Vec<u8>, u32>::new();
    let mut contexts = Vec::<Vec<u8>>::new();
    let mut encoded_rows = Vec::<Vec<u8>>::new();
    let mut packet_len = header_len;
    while encoded_rows.len() < usize::try_from(max_rows).unwrap_or(usize::MAX) {
        let row = if let Some(row) = cursor.pending_contextual_row.take() {
            Some(row)
        } else {
            residual.next_row(residual_space)?
        };
        let Some(row) = row else {
            break;
        };

        let context_mark = contexts.len();
        let mut encoded = Vec::new();
        if let Err(error) = append_contextual_indexed_query_packet_row(
            &mut encoded,
            &row,
            &mut context_ids,
            &mut contexts,
        ) {
            rollback_contextual_packet_contexts(&mut context_ids, &mut contexts, context_mark);
            return Err(error);
        }
        let new_context_bytes =
            contexts[context_mark..]
                .iter()
                .try_fold(0usize, |total, context| {
                    total
                        .checked_add(4usize)
                        .and_then(|value| value.checked_add(context.len()))
                        .ok_or_else(|| "indexed contextual packet size overflow".to_string())
                });
        let trial_len = new_context_bytes.and_then(|context_bytes| {
            packet_len
                .checked_add(context_bytes)
                .and_then(|value| value.checked_add(encoded.len()))
                .ok_or_else(|| "indexed contextual packet size overflow".to_string())
        });
        let trial_len = match trial_len {
            Ok(len) => len,
            Err(error) => {
                rollback_contextual_packet_contexts(&mut context_ids, &mut contexts, context_mark);
                return Err(error);
            }
        };
        if trial_len > max_bytes {
            rollback_contextual_packet_contexts(&mut context_ids, &mut contexts, context_mark);
            cursor.pending_contextual_row = Some(row);
            if encoded_rows.is_empty() {
                return Err("indexed contextual query row exceeds max_bytes".to_string());
            }
            break;
        }
        packet_len = trial_len;
        encoded_rows.push(encoded);
    }

    if encoded_rows.is_empty() {
        return Ok((Vec::new(), 0));
    }
    let row_count = checked_packet_count(
        encoded_rows.len(),
        "indexed contextual cursor batch row count",
    )?;
    let context_count = u32::try_from(contexts.len())
        .map_err(|_| "indexed contextual packet exceeded u32 context count".to_string())?;
    let mut packet = Vec::with_capacity(packet_len);
    append_contextual_indexed_query_rows_header(&mut packet, row_count, context_count);
    for (id, context) in contexts.iter().enumerate() {
        let context_id = u32::try_from(id)
            .map_err(|_| "indexed contextual packet exceeded u32 context id".to_string())?;
        append_opening_context(&mut packet, context_id, context);
    }
    for row in encoded_rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

fn query_cursor_next_packet(
    cursor: &mut BridgeQueryCursor,
    max_rows: u64,
    max_bytes: u64,
) -> Result<(Vec<u8>, u64), String> {
    if max_rows == 0 {
        return Err("query-row batch max_rows must be positive".to_string());
    }
    if max_bytes == 0 {
        return Err("query-row batch max_bytes must be positive".to_string());
    }
    if cursor.indexed_setup_stats.is_some() && !cursor.indexed_rows_available {
        return Err("indexed cursor supports aggregation but not row emission".to_string());
    }
    let max_bytes = usize::try_from(max_bytes).unwrap_or(usize::MAX);
    #[cfg(feature = "pathmap-space")]
    if matches!(
        &cursor.source,
        BridgeQueryCursorSource::IndexedExactResidual { .. }
    ) {
        return query_cursor_next_indexed_exact_residual_packet(cursor, max_rows, max_bytes);
    }
    let header_len = match cursor.kind {
        BridgeQueryCursorKind::QueryOnlyV2 => 16usize,
        BridgeQueryCursorKind::MultiRefV3 { .. } => 20usize,
    };
    if max_bytes <= header_len {
        return Err("query-row batch max_bytes is smaller than the packet header".to_string());
    }

    let mut selected_rows = Vec::<Vec<u8>>::new();
    let mut packet_len = header_len;
    while selected_rows.len() < usize::try_from(max_rows).unwrap_or(usize::MAX) {
        let mut capture_fresh_row = false;
        let row = if let Some(row) = cursor.pending_row.take() {
            Some(row)
        } else {
            match &mut cursor.source {
                BridgeQueryCursorSource::Materialized { rows, next_row } => {
                    if *next_row >= rows.len() {
                        None
                    } else {
                        let row = rows
                            .get(*next_row)
                            .expect("materialized cursor index must be in range")
                            .to_vec();
                        *next_row += 1;
                        Some(row)
                    }
                }
                BridgeQueryCursorSource::Flat(flat) => {
                    let row = flat.next_packet_row()?;
                    capture_fresh_row = row.is_some();
                    row
                }
                BridgeQueryCursorSource::IndexedExactResidual { .. } => {
                    return Err(
                        "indexed exact-residual cursor bypassed its packet protocol".to_string()
                    );
                }
                BridgeQueryCursorSource::GeneralCounted { space, cursor } => {
                    cursor.next_packet_row(space)?
                }
                BridgeQueryCursorSource::SemiNaive(semi_naive) => semi_naive.next_packet_row()?,
                BridgeQueryCursorSource::Replay {
                    rows,
                    next_row,
                    stats,
                } => {
                    if *next_row >= rows.len() {
                        None
                    } else {
                        let row = rows[*next_row].clone();
                        *next_row += 1;
                        stats.rows_emitted = stats.rows_emitted.saturating_add(1);
                        Some(row)
                    }
                }
            }
        };
        let Some(row) = row else {
            query_cursor_publish_completed_replay(cursor);
            break;
        };
        if capture_fresh_row {
            query_cursor_capture_row(cursor, &row);
        }
        if query_cursor_packet_would_exceed(packet_len, row.len(), max_bytes) {
            cursor.pending_row = Some(row);
            if selected_rows.is_empty() {
                return Err("query-row batch row exceeds max_bytes".to_string());
            }
            break;
        }
        packet_len += row.len();
        selected_rows.push(row);
    }

    if selected_rows.is_empty() {
        return Ok((Vec::new(), 0));
    }
    let row_count = checked_packet_count(selected_rows.len(), "query-row cursor batch row count")?;
    let mut packet = Vec::with_capacity(packet_len);
    match cursor.kind {
        BridgeQueryCursorKind::QueryOnlyV2 => {
            append_query_only_v2_header(&mut packet, row_count);
        }
        BridgeQueryCursorKind::MultiRefV3 { factor_count } => {
            append_multi_ref_v3_header(
                &mut packet,
                MULTI_REF_V3_FLAG_QUERY_KEYS_ONLY
                    | MULTI_REF_V3_FLAG_RAW_EXPR_BYTES
                    | MULTI_REF_V3_FLAG_DIRECT_MULTIPLICITIES
                    | MULTI_REF_V3_FLAG_WIDE_TOKENS,
                factor_count,
                row_count,
            );
        }
    }
    for row in selected_rows {
        packet.extend_from_slice(&row);
    }
    Ok((packet, row_count))
}

fn dump_bridge_space_text(bridge: &BridgeSpace) -> Result<(Vec<u8>, u64), String> {
    if bridge_uses_counted_storage(bridge) {
        return counted_sexpr_text(&bridge.inner);
    }
    let mut text = Vec::new();
    let mut count = 0u64;
    let mut rz = bridge.inner.btm.read_zipper();
    while rz.to_next_val() {
        let expr = Expr {
            ptr: rz.origin_path().as_ptr().cast_mut(),
        };
        text.extend_from_slice(&bridge_expr_text(&bridge.inner, expr)?);
        text.push(b'\n');
        count = count
            .checked_add(1)
            .ok_or_else(|| "bridge text row count overflow".to_string())?;
    }
    Ok((text, count))
}

fn dump_bridge_space_expr_rows(bridge: &BridgeSpace) -> Result<(Vec<u8>, u64), String> {
    if bridge_uses_counted_storage(bridge) {
        return counted_expr_row_packet(&bridge.inner);
    }

    let mut packet = Vec::new();
    let mut count = 0u64;

    let mut rz = bridge.inner.btm.read_zipper();
    while rz.to_next_val() {
        let expr = Expr {
            ptr: rz.origin_path().as_ptr().cast_mut(),
        };
        append_bridge_expr_bytes(&bridge.inner, &mut packet, expr)?;
        count = count
            .checked_add(1)
            .ok_or_else(|| "bridge expr row count overflow".to_string())?;
    }
    Ok((packet, count))
}

fn dump_bridge_space_contextual_exact_rows(bridge: &BridgeSpace) -> Result<(Vec<u8>, u64), String> {
    let mut rows: Vec<(u32, u32, Vec<u8>)> = Vec::new();
    let mut context_ids: BTreeMap<Vec<u8>, u32> = BTreeMap::new();
    let mut contexts: Vec<Vec<u8>> = Vec::new();
    let empty_context = 0u32.to_be_bytes().to_vec();

    let mut intern_context = |context: &[u8]| -> Result<u32, String> {
        if let Some(id) = context_ids.get(context) {
            return Ok(*id);
        }
        let id = u32::try_from(contexts.len())
            .map_err(|_| "contextual exact-row dump exceeded u32 context count".to_string())?;
        context_ids.insert(context.to_vec(), id);
        contexts.push(context.to_vec());
        Ok(id)
    };

    if bridge_uses_counted_storage(bridge) {
        for entry in counted_entries(&bridge.inner)? {
            let expr = Expr {
                ptr: entry.atom_expr_bytes.as_ptr().cast_mut(),
            };
            let encoded = stable_bridge_expr_packet_bytes(&bridge.inner, expr)?;
            let var_count = bridge_expr_packet_var_count(&encoded)?;
            if let Some(per_expr) = bridge.exact_contexts.get(&entry.atom_expr_bytes) {
                let mut covered = 0u32;
                for (context, count) in per_expr {
                    validate_contextual_exact_context(context, var_count)?;
                    let context_id = intern_context(context)?;
                    rows.push((context_id, *count, encoded.clone()));
                    covered = covered.checked_add(*count).ok_or_else(|| {
                        "contextual exact-row presentation counts overflowed".to_string()
                    })?;
                }
                if covered > entry.count {
                    return Err(
                        "contextual exact-row presentations exceed structural multiplicity"
                            .to_string(),
                    );
                }
                if covered < entry.count {
                    if var_count != 0 {
                        return Err(
                            "contextual exact-row dump is missing opening context for a variable-bearing row"
                                .to_string(),
                        );
                    }
                    let context_id = intern_context(&empty_context)?;
                    rows.push((context_id, entry.count - covered, encoded));
                }
            } else if var_count != 0 {
                return Err(
                    "contextual exact-row dump needs an opening context for variable-bearing PathMap rows"
                        .to_string(),
                );
            } else {
                let context_id = intern_context(&empty_context)?;
                rows.push((context_id, entry.count, encoded));
            }
        }
    } else {
        let mut rz = bridge.inner.btm.read_zipper();
        while rz.to_next_val() {
            let expr = Expr {
                ptr: rz.origin_path().as_ptr().cast_mut(),
            };
            let encoded = stable_bridge_expr_packet_bytes(&bridge.inner, expr)?;
            let var_count = bridge_expr_packet_var_count(&encoded)?;
            if var_count != 0 {
                return Err(
                    "contextual exact-row dump needs an opening context for variable-bearing MORK rows"
                        .to_string(),
                );
            }
            let context_id = intern_context(&empty_context)?;
            rows.push((context_id, 1, encoded));
        }
    }

    let row_count = checked_packet_count(rows.len(), "contextual exact-row dump row count")?;
    let mut packet = Vec::new();
    let context_count = u32::try_from(contexts.len())
        .map_err(|_| "contextual exact-row dump exceeded u32 context count".to_string())?;
    append_contextual_exact_rows_header(&mut packet, row_count, context_count);
    for (id, context) in contexts.iter().enumerate() {
        append_opening_context(&mut packet, id as u32, context);
    }
    for (context_id, multiplicity, encoded) in rows {
        append_contextual_exact_row(&mut packet, context_id, multiplicity, &encoded)?;
    }
    Ok((packet, row_count))
}

// === Space lifecycle, mutation, and algebra FFI ===

/// Allocates a fresh bridge-owned MORK space and returns it as an opaque C handle.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_new() -> *mut MorkSpace {
    with_catch(|| {
        let bridge = Box::new(BridgeSpace {
            inner: Space::new(),
            storage_mode: BridgeStorageMode::RawExprs,
            counted_logical_size: 0,
            exact_contexts: HashMap::new(),
            flat_query_index: Arc::new(FlatCountedQueryIndex::default()),
            query_replay_cache: Arc::new(Mutex::new(QueryReplayCache::default())),
            query_revision: 0,
            counted_version: counted_version_root(),
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
            counted_logical_size: 0,
            exact_contexts: HashMap::new(),
            flat_query_index: Arc::new(FlatCountedQueryIndex::default()),
            query_replay_cache: Arc::new(Mutex::new(QueryReplayCache::default())),
            query_revision: 0,
            counted_version: counted_version_root(),
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
        bridge.counted_logical_size = 0;
        bridge.exact_contexts.clear();
        bridge_reset_flat_query_index(bridge);
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
                Ok(exprs) => match bridge_counted_insert_exprs_transaction(bridge, &exprs) {
                    Ok(added) => MorkStatus::ok(added),
                    Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
                },
                Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
            }
        } else {
            match parse_expr_chunk(&mut bridge.inner, bytes) {
                Ok(exprs) => {
                    for expr in &exprs {
                        bridge.inner.btm.insert(expr, ());
                    }
                    MorkStatus::ok(exprs.len() as u64)
                }
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

/// Converts one length-delimited, symbol-table-independent expression packet
/// into the compact expression bytes owned by this space. Long symbols are
/// interned in the target space during normalization.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_normalize_expr_packet(
    space: *mut MorkSpace,
    packet: *const u8,
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
        if packet.is_null() {
            return MorkBuffer::err(MorkStatusCode::Null, b"null bridge expr packet".to_vec());
        }
        let packet = std::slice::from_raw_parts(packet, len);
        match bridge_expr_packet_to_bytes(&bridge.inner, packet) {
            Ok(expr_bytes) => MorkBuffer::ok(expr_bytes, 1),
            Err(err) => MorkBuffer::err(MorkStatusCode::Parse, err.into_bytes()),
        }
    })
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
            match counted_insert_expr_cached(
                &mut bridge.inner,
                expr_bytes,
                &mut bridge.counted_logical_size,
            ) {
                Ok(_) => {
                    bridge_note_counted_single_change(bridge, expr_bytes, 1);
                    MorkStatus::ok(1)
                }
                Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
            }
        } else {
            bridge.inner.btm.insert(expr_bytes, ());
            MorkStatus::ok(1)
        }
    })
}

/// Adds one encoded expression with an exact opening context for projection.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_add_contextual_exact_expr_bytes(
    space: *mut MorkSpace,
    expr_bytes: *const u8,
    len: usize,
    context_bytes: *const u8,
    context_len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if !bridge_uses_counted_storage(bridge) {
            return MorkStatus::err(
                MorkStatusCode::Internal,
                b"contextual exact rows require counted PathMap storage".to_vec(),
            );
        }
        if expr_bytes.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null expr bytes".to_vec());
        }
        if context_bytes.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null opening context".to_vec());
        }
        let expr_bytes = std::slice::from_raw_parts(expr_bytes, len);
        let context_bytes = std::slice::from_raw_parts(context_bytes, context_len);
        if let Err(err) = validate_expr_bytes(expr_bytes) {
            return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes());
        }
        let expr = Expr {
            ptr: expr_bytes.as_ptr().cast_mut(),
        };
        let encoded = match stable_bridge_expr_packet_bytes(&bridge.inner, expr) {
            Ok(encoded) => encoded,
            Err(err) => return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        };
        let var_count = match bridge_expr_packet_var_count(&encoded) {
            Ok(count) => count,
            Err(err) => return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        };
        if let Err(err) = validate_contextual_exact_context(context_bytes, var_count) {
            return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes());
        }
        if let Err(err) =
            ensure_exact_context_count_can_add(&bridge.exact_contexts, expr_bytes, context_bytes, 1)
        {
            return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes());
        }
        match counted_insert_expr_cached(
            &mut bridge.inner,
            expr_bytes,
            &mut bridge.counted_logical_size,
        ) {
            Ok(_) => {
                if let Err(err) = increment_exact_context_count(
                    &mut bridge.exact_contexts,
                    expr_bytes,
                    context_bytes,
                ) {
                    return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes());
                }
                bridge_note_counted_single_change(bridge, expr_bytes, 1);
                MorkStatus::ok(1)
            }
            Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
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
            match bridge_counted_insert_exprs_transaction(bridge, &exprs) {
                Ok(added) => MorkStatus::ok(added),
                Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
            }
        } else {
            for expr in &exprs {
                bridge.inner.btm.insert(expr, ());
            }
            MorkStatus::ok(exprs.len() as u64)
        }
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
                Ok(exprs) => match bridge_counted_remove_exprs_transaction(bridge, &exprs) {
                    Ok(removed) => MorkStatus::ok(removed),
                    Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
                },
                Err(err) => MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
            }
        } else {
            match parse_expr_chunk(&mut bridge.inner, bytes) {
                Ok(exprs) => {
                    let mut removed = 0u64;
                    for expr in &exprs {
                        removed += u64::from(bridge.inner.btm.remove(expr).is_some());
                    }
                    MorkStatus::ok(removed)
                }
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
            match counted_remove_one_expr_cached(
                &mut bridge.inner,
                expr_bytes,
                &mut bridge.counted_logical_size,
            ) {
                Ok(Some(_)) => {
                    decrement_any_exact_context_count(&mut bridge.exact_contexts, expr_bytes);
                    bridge_note_counted_single_change(bridge, expr_bytes, -1);
                    MorkStatus::ok(1)
                }
                Ok(None) => MorkStatus::ok(0),
                Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
            }
        } else {
            let removed = bridge.inner.btm.remove(expr_bytes).is_some();
            MorkStatus::ok(u64::from(removed))
        }
    })
}

/// Removes a packed batch of stable bridge expressions. Counted storage mutates
/// a copy-on-write trial and publishes it once, so readers observe either the
/// old or the new bag.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_remove_expr_bytes_batch(
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
            match bridge_counted_remove_exprs_transaction(bridge, &exprs) {
                Ok(removed) => MorkStatus::ok(removed),
                Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
            }
        } else {
            let mut removed = 0u64;
            for expr in &exprs {
                removed += u64::from(bridge.inner.btm.remove(expr).is_some());
            }
            MorkStatus::ok(removed)
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_remove_contextual_exact_expr_bytes(
    space: *mut MorkSpace,
    expr_bytes: *const u8,
    len: usize,
    context_bytes: *const u8,
    context_len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if !bridge_uses_counted_storage(bridge) {
            return MorkStatus::err(
                MorkStatusCode::Internal,
                b"contextual exact rows require counted PathMap storage".to_vec(),
            );
        }
        if expr_bytes.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null expr bytes".to_vec());
        }
        if context_bytes.is_null() {
            return MorkStatus::err(MorkStatusCode::Null, b"null opening context".to_vec());
        }
        let expr_bytes = std::slice::from_raw_parts(expr_bytes, len);
        let context_bytes = std::slice::from_raw_parts(context_bytes, context_len);
        if let Err(err) = validate_expr_bytes(expr_bytes) {
            return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes());
        }
        let expr = Expr {
            ptr: expr_bytes.as_ptr().cast_mut(),
        };
        let encoded = match stable_bridge_expr_packet_bytes(&bridge.inner, expr) {
            Ok(encoded) => encoded,
            Err(err) => return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        };
        let var_count = match bridge_expr_packet_var_count(&encoded) {
            Ok(count) => count,
            Err(err) => return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes()),
        };
        if let Err(err) = validate_contextual_exact_context(context_bytes, var_count) {
            return MorkStatus::err(MorkStatusCode::Parse, err.into_bytes());
        }
        if !has_exact_context_count(&bridge.exact_contexts, expr_bytes, context_bytes) {
            return MorkStatus::ok(0);
        }
        match counted_remove_one_expr_cached(
            &mut bridge.inner,
            expr_bytes,
            &mut bridge.counted_logical_size,
        ) {
            Ok(Some(_)) => {
                if !decrement_exact_context_count(
                    &mut bridge.exact_contexts,
                    expr_bytes,
                    context_bytes,
                ) {
                    return MorkStatus::err(
                        MorkStatusCode::Internal,
                        b"contextual exact context disappeared during remove".to_vec(),
                    );
                }
                bridge_note_counted_single_change(bridge, expr_bytes, -1);
                MorkStatus::ok(1)
            }
            Ok(None) => MorkStatus::err(
                MorkStatusCode::Internal,
                b"contextual exact context existed without structural row".to_vec(),
            ),
            Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_contains_expr_bytes(
    space: *const MorkSpace,
    expr_bytes: *const u8,
    len: usize,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_ref(space) {
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
        let found = if bridge_uses_counted_storage(bridge) {
            match counted_contains_expr(&bridge.inner, expr_bytes) {
                Ok(found) => found,
                Err(err) => return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
            }
        } else {
            bridge.inner.btm.read_zipper_at_path(expr_bytes).is_val()
        };
        MorkStatus::ok(u64::from(found))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_size(space: *const MorkSpace) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        MorkStatus::ok(bridge_stored_atom_count(bridge))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_space_unique_size(space: *const MorkSpace) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        let unique = if bridge_uses_counted_storage(bridge) {
            counted_unique_size(&bridge.inner)
        } else {
            bridge.inner.btm.val_count() as u64
        };
        MorkStatus::ok(unique)
    })
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
        match bridge_space_dump_act_transactional(bridge, &path) {
            Ok(()) => MorkStatus::ok(bridge_stored_atom_count(bridge)),
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
        match bridge_space_load_act_replacement(bridge.storage_mode, &path) {
            Ok(loaded) => {
                *bridge = loaded;
                MorkStatus::ok(bridge_stored_atom_count(bridge))
            }
            Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
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

/// Dumps the current space as repeated stable bridge expr-byte rows.
///
/// Packet format:
///   repeated rows:
///     u32 expr_len_be
///     u8[expr_len] expr_bytes
///
/// `count` reports the logical row count, so counted PathMap storage expands
/// multiplicities into repeated rows without requiring a staged textual dump.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_dump_expr_rows(space: *mut MorkSpace) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_space_ref(space) {
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
        match dump_bridge_space_expr_rows(bridge) {
            Ok((packet, count)) => MorkBuffer::ok(packet, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

/// Dumps the current space as the proposed contextual context-table exact-row packet.
///
/// Ground rows use empty opening contexts. Variable-bearing rows are emitted
/// only when they were imported with exact opening-context entries from CeTTa;
/// structural-only variable rows are rejected instead of being opened with
/// synthesized identities.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_dump_contextual_exact_rows(space: *mut MorkSpace) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_space_ref(space) {
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
        match dump_bridge_space_contextual_exact_rows(bridge) {
            Ok((packet, count)) => MorkBuffer::ok(packet, count),
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

/// Returns the exact positive counted delta between an earlier snapshot and a
/// descendant, or null when the lineage includes removal, reset, or an opaque
/// bulk mutation.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_monotone_delta(
    later: *const MorkSpace,
    earlier: *const MorkSpace,
) -> *mut MorkSpace {
    with_catch(|| unsafe {
        let later = match bridge_space_ref(later) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let earlier = match bridge_space_ref(earlier) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        match bridge_monotone_delta_space(later, earlier) {
            Ok(delta) => Box::into_raw(Box::new(delta)) as *mut MorkSpace,
            Err(_) => ptr::null_mut(),
        }
    })
}

/// Adds each logical source row to the destination using the destination's
/// storage mode, without materializing through UTF-8 text or CeTTa atoms.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_add_logical_rows_from(
    dst: *mut MorkSpace,
    src: *const MorkSpace,
) -> MorkStatus {
    with_catch_status(|| unsafe { bridge_space_add_logical_rows_from_raw(dst, src) })
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
        match clone_then_mutate(lhs, rhs, bridge_space_join_into) {
            Ok(space) => space,
            Err(_) => ptr::null_mut(),
        }
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
        match clone_then_mutate(lhs, rhs, bridge_space_meet_into) {
            Ok(space) => space,
            Err(_) => ptr::null_mut(),
        }
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
        match clone_then_mutate(lhs, rhs, bridge_space_subtract_into) {
            Ok(space) => space,
            Err(_) => ptr::null_mut(),
        }
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
        match clone_then_mutate(lhs, rhs, bridge_space_restrict_into) {
            Ok(space) => space,
            Err(_) => ptr::null_mut(),
        }
    })
}

// === Single-space cursor FFI ===

fn bridge_cursor_counted_expr_rows(
    bridge: &BridgeSpace,
) -> Result<Option<BridgeCountedCursorRows>, String> {
    if !bridge_uses_counted_storage(bridge) {
        return Ok(None);
    }
    let entries = counted_entries(&bridge.inner)?
        .into_iter()
        .map(|entry| (entry.atom_expr_bytes, entry.count))
        .collect();
    Ok(Some(BridgeCountedCursorRows {
        entries,
        entry_index: 0,
        emitted_from_entry: 0,
    }))
}

/// Creates a read-only cursor snapshot over one space.
#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_new(space: *const MorkSpace) -> *mut MorkCursor {
    with_catch(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let counted_expr_rows = match bridge_cursor_counted_expr_rows(bridge) {
            Ok(rows) => rows,
            Err(_) => return ptr::null_mut(),
        };
        let cursor = Box::new(BridgeCursor {
            space: clone_space_inner(&bridge.inner),
            storage_mode: bridge.storage_mode,
            path: Vec::new(),
            raw_expr_rows_started: false,
            counted_expr_rows,
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        MorkBuffer::ok(bridge.path.clone(), 0)
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
        let mut rz = bridge.space.btm.read_zipper();
        rz.descend_to(&bridge.path);
        let child_bytes = rz.child_mask().iter().collect::<Vec<u8>>();
        MorkBuffer::ok(child_bytes, 0)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_val_count(cursor: *const MorkCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
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
        let mut rz = bridge.space.btm.read_zipper();
        rz.descend_to(&bridge.path);
        let moved = rz.to_next_val();
        bridge.path = rz.path().to_vec();
        MorkStatus::ok(moved as u64)
    })
}

fn bridge_packet_would_exceed(packet_len: usize, row_len: usize, max_bytes: usize) -> bool {
    packet_len != 0
        && packet_len
            .checked_add(row_len)
            .map(|next_len| next_len > max_bytes)
            .unwrap_or(true)
}

fn bridge_cursor_next_raw_expr_rows(
    space: &Space,
    path: &mut Vec<u8>,
    started: &mut bool,
    max_rows: u64,
    max_bytes: usize,
) -> Result<(Vec<u8>, u64), String> {
    let mut packet = Vec::new();
    let mut rows = 0u64;
    let mut rz = space.btm.read_zipper();
    rz.descend_to(path.as_slice());
    let resume_from = path.clone();
    let mut need_resume_skip = *started;

    while rows < max_rows {
        let old_path = path.clone();
        let moved = if need_resume_skip {
            let mut found = false;
            while rz.to_next_val() {
                let origin = rz.origin_path();
                if origin != resume_from.as_slice() && !origin.starts_with(resume_from.as_slice()) {
                    found = true;
                    break;
                }
            }
            need_resume_skip = false;
            found
        } else {
            rz.to_next_val()
        };
        *path = rz.path().to_vec();
        if !moved {
            break;
        }

        let expr = Expr {
            ptr: rz.origin_path().as_ptr().cast_mut(),
        };
        let mut row = Vec::new();
        append_bridge_expr_bytes(space, &mut row, expr)?;
        if bridge_packet_would_exceed(packet.len(), row.len(), max_bytes) {
            *path = old_path;
            break;
        }
        packet.extend_from_slice(&row);
        *started = true;
        rows = rows
            .checked_add(1)
            .ok_or_else(|| "cursor expr-row batch count overflow".to_string())?;
    }

    Ok((packet, rows))
}

fn bridge_cursor_next_counted_expr_rows(
    space: &Space,
    state: &mut BridgeCountedCursorRows,
    max_rows: u64,
    max_bytes: usize,
) -> Result<(Vec<u8>, u64), String> {
    let mut packet = Vec::new();
    let mut rows = 0u64;

    while rows < max_rows && state.entry_index < state.entries.len() {
        let (expr_bytes, multiplicity) = &state.entries[state.entry_index];
        if state.emitted_from_entry >= *multiplicity {
            state.entry_index += 1;
            state.emitted_from_entry = 0;
            continue;
        }

        let expr = Expr {
            ptr: expr_bytes.as_ptr().cast_mut(),
        };
        let mut row = Vec::new();
        append_bridge_expr_bytes(space, &mut row, expr)?;
        if bridge_packet_would_exceed(packet.len(), row.len(), max_bytes) {
            break;
        }

        packet.extend_from_slice(&row);
        state.emitted_from_entry += 1;
        rows = rows
            .checked_add(1)
            .ok_or_else(|| "cursor counted expr-row batch count overflow".to_string())?;
    }

    Ok((packet, rows))
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_cursor_next_expr_rows(
    cursor: *mut MorkCursor,
    max_rows: u64,
    max_bytes: u64,
) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let bridge = match bridge_cursor_mut(cursor) {
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
        if max_rows == 0 {
            return MorkBuffer::err(
                MorkStatusCode::Parse,
                b"cursor expr-row batch max_rows must be positive".to_vec(),
            );
        }
        if max_bytes == 0 {
            return MorkBuffer::err(
                MorkStatusCode::Parse,
                b"cursor expr-row batch max_bytes must be positive".to_vec(),
            );
        }
        let max_bytes = usize::try_from(max_bytes).unwrap_or(usize::MAX);
        let result = if bridge.storage_mode == BridgeStorageMode::CountedPathmap {
            let state = match bridge.counted_expr_rows.as_mut() {
                Some(state) => state,
                None => {
                    return MorkBuffer::err(
                        MorkStatusCode::Internal,
                        b"counted cursor row state is missing".to_vec(),
                    );
                }
            };
            bridge_cursor_next_counted_expr_rows(&bridge.space, state, max_rows, max_bytes)
        } else {
            bridge_cursor_next_raw_expr_rows(
                &bridge.space,
                &mut bridge.path,
                &mut bridge.raw_expr_rows_started,
                max_rows,
                max_bytes,
            )
        };
        match result {
            Ok((packet, rows)) => MorkBuffer::ok(packet, rows),
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
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
            space: clone_space_inner(&bridge.space),
            storage_mode: bridge.storage_mode,
            path: bridge.path.clone(),
            raw_expr_rows_started: bridge.raw_expr_rows_started,
            counted_expr_rows: bridge.counted_expr_rows.clone(),
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
        let snapshot = match cursor_structural_from_focus(&bridge.space.btm, &bridge.path) {
            Ok(snapshot) => snapshot,
            Err(_) => return ptr::null_mut(),
        };
        match bridge_space_from_snapshot(snapshot) {
            Ok(space) => space,
            Err(_) => ptr::null_mut(),
        }
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
        let snapshot = match cursor_snapshot_from_focus(&bridge.space.btm, &bridge.path) {
            Ok(snapshot) => snapshot,
            Err(_) => return ptr::null_mut(),
        };
        match bridge_space_from_snapshot(snapshot) {
            Ok(space) => space,
            Err(_) => ptr::null_mut(),
        }
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
        MorkBuffer::ok(bridge.path.clone(), 0)
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
        MorkBuffer::ok(child_bytes, 0)
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
        let count = match checked_packet_count(indices.len(), "product cursor path-index count") {
            Ok(count) => count,
            Err(err) => return MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        };
        MorkBuffer::ok(encode_u64_list(&indices), count)
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
        MorkBuffer::ok(bridge.path.clone(), 0)
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
        MorkBuffer::ok(child_bytes, 0)
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
pub extern "C" fn mork_query_cursor_new_query_only_v2(
    space: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> *mut MorkQueryCursor {
    with_catch(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        if pattern.is_null() {
            return ptr::null_mut();
        }
        let mut owned = bridge_space_clone_owned(bridge);
        let pattern = std::slice::from_raw_parts(pattern, len);
        let (_row_count, rows) =
            match query_bindings_query_only_v2_chunked_rows(&mut owned, pattern) {
                Ok(rows) => rows,
                Err(_) => return ptr::null_mut(),
            };
        let cursor = Box::new(BridgeQueryCursor {
            kind: BridgeQueryCursorKind::QueryOnlyV2,
            source: BridgeQueryCursorSource::Materialized { rows, next_row: 0 },
            pending_row: None,
            pending_contextual_row: None,
            indexed_setup_stats: None,
            indexed_query_revision: 0,
            indexed_has_residual: false,
            indexed_has_exact_partition: false,
            indexed_rows_available: false,
            replay_hit: false,
            replay_capture: None,
        });
        Box::into_raw(cursor) as *mut MorkQueryCursor
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_query_cursor_new_multi_ref_v3(
    space: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> *mut MorkQueryCursor {
    with_catch(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        if pattern.is_null() {
            return ptr::null_mut();
        }
        if !bridge_uses_counted_storage(bridge) {
            return ptr::null_mut();
        }
        let mut owned = bridge_space_clone_owned(bridge);
        let pattern = std::slice::from_raw_parts(pattern, len);
        let normalized = match normalize_query_text(pattern) {
            Ok(normalized) => normalized,
            Err(_) => return ptr::null_mut(),
        };
        let pattern_bytes = match parse_single_expr(&mut owned.inner, &normalized) {
            Ok(pattern) => pattern,
            Err(_) => return ptr::null_mut(),
        };
        let general = match CountedGeneralQueryCursor::new(&owned.inner, &pattern_bytes) {
            Ok(cursor) => cursor,
            Err(_) => return ptr::null_mut(),
        };
        let factor_count = general.factor_count();
        let cursor = Box::new(BridgeQueryCursor {
            kind: BridgeQueryCursorKind::MultiRefV3 { factor_count },
            source: BridgeQueryCursorSource::GeneralCounted {
                space: owned.inner,
                cursor: general,
            },
            pending_row: None,
            pending_contextual_row: None,
            indexed_setup_stats: None,
            indexed_query_revision: 0,
            indexed_has_residual: false,
            indexed_has_exact_partition: false,
            indexed_rows_available: false,
            replay_hit: false,
            replay_capture: None,
        });
        Box::into_raw(cursor) as *mut MorkQueryCursor
    })
}

/// Creates a genuinely pull-based counted-PathMap cursor for the admitted flat
/// relational fragment. A non-null cursor may support aggregation without row
/// emission; row consumers must inspect `MORK_INDEXED_CURSOR_STAT_ROWS_AVAILABLE`.
/// A null result means that the exact general-query path must be used; it is not
/// a query failure.
#[unsafe(no_mangle)]
pub extern "C" fn mork_query_cursor_new_indexed_multi_ref_v4(
    space: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> *mut MorkQueryCursor {
    with_catch(|| unsafe {
        let bridge = match bridge_space_mut(space) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        if !bridge_uses_counted_storage(bridge) || pattern.is_null() {
            return ptr::null_mut();
        }
        let pattern = std::slice::from_raw_parts(pattern, len);
        let normalized = match normalize_query_text(pattern) {
            Ok(normalized) => normalized,
            Err(_) => return ptr::null_mut(),
        };
        let pattern_bytes = match parse_single_expr(&mut bridge.inner, &normalized) {
            Ok(pattern) => pattern,
            Err(_) => return ptr::null_mut(),
        };
        let before = bridge.flat_query_index.stats().clone();
        let admission = match Arc::make_mut(&mut bridge.flat_query_index)
            .prepare(&bridge.inner, &pattern_bytes)
        {
            Ok(admission) => admission,
            Err(_) => {
                bridge.flat_query_index = Arc::new(FlatCountedQueryIndex::default());
                return ptr::null_mut();
            }
        };
        let (key, factor_count, has_residual, has_exact_partition) = match admission {
            FlatCountedQueryAdmission::Prepared {
                key,
                factor_count,
                has_residual,
                has_exact_partition,
            } => (key, factor_count, has_residual, has_exact_partition),
            FlatCountedQueryAdmission::Unsupported { .. } => return ptr::null_mut(),
        };
        let replay_key = (bridge.query_revision, key.clone());
        let replay_rows = if has_residual {
            None
        } else {
            query_replay_lookup(&bridge.query_replay_cache, &replay_key)
        };
        let (source, replay_hit, replay_capture) = if let Some(rows) = replay_rows {
            (
                BridgeQueryCursorSource::Replay {
                    rows,
                    next_row: 0,
                    stats: FlatCountedCursorStats::default(),
                },
                true,
                None,
            )
        } else {
            let flat = match FlatCountedQueryCursor::new(bridge.flat_query_index.clone(), &key) {
                Ok(cursor) => cursor,
                Err(_) => return ptr::null_mut(),
            };
            let source = if has_residual {
                let residual_lists = match bridge.flat_query_index.residual_candidate_lists(
                    &bridge.inner,
                    &pattern_bytes,
                    &key,
                ) {
                    Ok(Some(lists)) => lists,
                    Ok(None) | Err(_) => return ptr::null_mut(),
                };
                let contextual_lists = match residual_lists
                    .into_iter()
                    .map(|entries| {
                        contextual_query_candidates_from_counted_entries(bridge, entries)
                    })
                    .collect::<Result<Vec<_>, _>>()
                {
                    Ok(lists) => lists,
                    Err(_) => return ptr::null_mut(),
                };
                let residual =
                    match ContextualResidualQueryCursor::new(&pattern_bytes, contextual_lists) {
                        Ok(cursor) => cursor,
                        Err(_) => return ptr::null_mut(),
                    };
                BridgeQueryCursorSource::IndexedExactResidual {
                    exact: flat,
                    exact_done: false,
                    residual_space: clone_space_inner(&bridge.inner),
                    residual,
                }
            } else {
                BridgeQueryCursorSource::Flat(flat)
            };
            let capture = if has_residual {
                None
            } else {
                Some(QueryReplayCapture {
                    cache: bridge.query_replay_cache.clone(),
                    key: replay_key,
                    rows: Vec::new(),
                    bytes: 0,
                    eligible: true,
                })
            };
            (source, false, capture)
        };
        let after = bridge.flat_query_index.stats();
        let indexed_setup_stats = FlatCountedIndexStats {
            catalog_builds: after.catalog_builds.saturating_sub(before.catalog_builds),
            catalog_rows_scanned: after
                .catalog_rows_scanned
                .saturating_sub(before.catalog_rows_scanned),
            access_path_builds: after
                .access_path_builds
                .saturating_sub(before.access_path_builds),
            access_path_rows_indexed: after
                .access_path_rows_indexed
                .saturating_sub(before.access_path_rows_indexed),
            incremental_updates: after
                .incremental_updates
                .saturating_sub(before.incremental_updates),
            plan_builds: after.plan_builds.saturating_sub(before.plan_builds),
            plan_cache_hits: after.plan_cache_hits.saturating_sub(before.plan_cache_hits),
        };
        let indexed_rows_available = match &source {
            BridgeQueryCursorSource::IndexedExactResidual { residual, .. } => {
                residual.rows_available()
            }
            _ => true,
        };
        let cursor = Box::new(BridgeQueryCursor {
            kind: BridgeQueryCursorKind::MultiRefV3 { factor_count },
            source,
            pending_row: None,
            pending_contextual_row: None,
            indexed_setup_stats: Some(indexed_setup_stats),
            indexed_query_revision: bridge.query_revision,
            indexed_has_residual: has_residual,
            indexed_has_exact_partition: has_exact_partition,
            indexed_rows_available,
            replay_hit,
            replay_capture,
        });
        Box::into_raw(cursor) as *mut MorkQueryCursor
    })
}

fn flat_index_stats_delta(
    after: &FlatCountedIndexStats,
    before: &FlatCountedIndexStats,
) -> FlatCountedIndexStats {
    FlatCountedIndexStats {
        catalog_builds: after.catalog_builds.saturating_sub(before.catalog_builds),
        catalog_rows_scanned: after
            .catalog_rows_scanned
            .saturating_sub(before.catalog_rows_scanned),
        access_path_builds: after
            .access_path_builds
            .saturating_sub(before.access_path_builds),
        access_path_rows_indexed: after
            .access_path_rows_indexed
            .saturating_sub(before.access_path_rows_indexed),
        incremental_updates: after
            .incremental_updates
            .saturating_sub(before.incremental_updates),
        plan_builds: after.plan_builds.saturating_sub(before.plan_builds),
        plan_cache_hits: after.plan_cache_hits.saturating_sub(before.plan_cache_hits),
    }
}

fn flat_index_stats_add(lhs: &mut FlatCountedIndexStats, rhs: &FlatCountedIndexStats) {
    lhs.catalog_builds = lhs.catalog_builds.saturating_add(rhs.catalog_builds);
    lhs.catalog_rows_scanned = lhs
        .catalog_rows_scanned
        .saturating_add(rhs.catalog_rows_scanned);
    lhs.access_path_builds = lhs
        .access_path_builds
        .saturating_add(rhs.access_path_builds);
    lhs.access_path_rows_indexed = lhs
        .access_path_rows_indexed
        .saturating_add(rhs.access_path_rows_indexed);
    lhs.incremental_updates = lhs
        .incremental_updates
        .saturating_add(rhs.incremental_updates);
    lhs.plan_builds = lhs.plan_builds.saturating_add(rhs.plan_builds);
    lhs.plan_cache_hits = lhs.plan_cache_hits.saturating_add(rhs.plan_cache_hits);
}

/// Creates a pull cursor over the exact semi-naive first-delta partition.
///
/// For pivot `i`, factors before `i` read the old snapshot, factor `i` reads
/// the positive delta, and later factors read the current known space. The
/// variants are disjoint by construction, preserving bag multiplicity.
#[unsafe(no_mangle)]
pub extern "C" fn mork_query_cursor_new_indexed_semi_naive_multi_ref_v4(
    known: *mut MorkSpace,
    old: *mut MorkSpace,
    delta: *mut MorkSpace,
    pattern: *const u8,
    len: usize,
) -> *mut MorkQueryCursor {
    with_catch(|| unsafe {
        if known.is_null()
            || old.is_null()
            || delta.is_null()
            || pattern.is_null()
            || known == old
            || known == delta
            || old == delta
        {
            return ptr::null_mut();
        }
        let known = match bridge_space_mut(known) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let old = match bridge_space_mut(old) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        let delta = match bridge_space_mut(delta) {
            Ok(space) => space,
            Err(_) => return ptr::null_mut(),
        };
        if !bridge_uses_counted_storage(known)
            || !bridge_uses_counted_storage(old)
            || !bridge_uses_counted_storage(delta)
        {
            return ptr::null_mut();
        }

        let pattern = std::slice::from_raw_parts(pattern, len);
        let normalized = match normalize_query_text(pattern) {
            Ok(normalized) => normalized,
            Err(_) => return ptr::null_mut(),
        };
        let pattern_bytes = match parse_single_expr(&mut known.inner, &normalized) {
            Ok(pattern) => pattern,
            Err(_) => return ptr::null_mut(),
        };

        let known_before = known.flat_query_index.stats().clone();
        let old_before = old.flat_query_index.stats().clone();
        let delta_before = delta.flat_query_index.stats().clone();
        let admission = match Arc::make_mut(&mut known.flat_query_index)
            .prepare(&known.inner, &pattern_bytes)
        {
            Ok(admission) => admission,
            Err(_) => {
                known.flat_query_index = Arc::new(FlatCountedQueryIndex::default());
                return ptr::null_mut();
            }
        };
        let (key, factor_count) = match admission {
            FlatCountedQueryAdmission::Prepared {
                key,
                factor_count,
                has_residual: false,
                ..
            } => (key, factor_count),
            FlatCountedQueryAdmission::Prepared {
                has_residual: true, ..
            } => return ptr::null_mut(),
            FlatCountedQueryAdmission::Unsupported { .. } => return ptr::null_mut(),
        };
        let known_index = known.flat_query_index.clone();
        let semi_naive = match FlatSemiNaiveQueryCursor::new(
            known_index,
            &old.inner,
            &mut old.flat_query_index,
            &delta.inner,
            &mut delta.flat_query_index,
            &key,
        ) {
            Ok(cursor) => cursor,
            Err(_) => return ptr::null_mut(),
        };

        let mut setup = flat_index_stats_delta(known.flat_query_index.stats(), &known_before);
        flat_index_stats_add(
            &mut setup,
            &flat_index_stats_delta(old.flat_query_index.stats(), &old_before),
        );
        flat_index_stats_add(
            &mut setup,
            &flat_index_stats_delta(delta.flat_query_index.stats(), &delta_before),
        );
        let cursor = Box::new(BridgeQueryCursor {
            kind: BridgeQueryCursorKind::MultiRefV3 { factor_count },
            source: BridgeQueryCursorSource::SemiNaive(semi_naive),
            pending_row: None,
            pending_contextual_row: None,
            indexed_setup_stats: Some(setup),
            indexed_query_revision: known.query_revision,
            indexed_has_residual: false,
            indexed_has_exact_partition: true,
            indexed_rows_available: true,
            replay_hit: false,
            replay_capture: None,
        });
        Box::into_raw(cursor) as *mut MorkQueryCursor
    })
}

/// Returns one cumulative counted-index statistic for complexity gates.
///
/// Statistic ids are the `MORK_INDEXED_SPACE_STAT_*` constants in the C
/// header. Unknown ids fail instead of silently returning zero.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_indexed_query_stat(space: *const MorkSpace, stat: u32) -> MorkStatus {
    with_catch_status(|| unsafe {
        let bridge = match bridge_space_ref(space) {
            Ok(space) => space,
            Err(err) => return err,
        };
        if !bridge_uses_counted_storage(bridge) {
            return MorkStatus::err(
                MorkStatusCode::Internal,
                b"indexed query statistics require counted PathMap storage".to_vec(),
            );
        }
        let stats = bridge.flat_query_index.stats();
        let replay = match bridge.query_replay_cache.lock() {
            Ok(replay) => replay,
            Err(_) => {
                return MorkStatus::err(
                    MorkStatusCode::Internal,
                    b"indexed query replay cache lock is poisoned".to_vec(),
                );
            }
        };
        let value = match stat {
            INDEXED_SPACE_STAT_QUERY_REVISION => bridge.query_revision,
            INDEXED_SPACE_STAT_CATALOG_BUILT => {
                u64::from(bridge.flat_query_index.is_catalog_built())
            }
            INDEXED_SPACE_STAT_CATALOG_BUILDS => stats.catalog_builds,
            INDEXED_SPACE_STAT_CATALOG_ROWS_SCANNED => stats.catalog_rows_scanned,
            INDEXED_SPACE_STAT_ACCESS_PATH_BUILDS => stats.access_path_builds,
            INDEXED_SPACE_STAT_ACCESS_PATH_ROWS_INDEXED => stats.access_path_rows_indexed,
            INDEXED_SPACE_STAT_INCREMENTAL_UPDATES => stats.incremental_updates,
            INDEXED_SPACE_STAT_PLAN_BUILDS => stats.plan_builds,
            INDEXED_SPACE_STAT_PLAN_CACHE_HITS => stats.plan_cache_hits,
            INDEXED_SPACE_STAT_REPLAY_COMPLETIONS => replay.completions,
            INDEXED_SPACE_STAT_REPLAY_HITS => replay.hits,
            INDEXED_SPACE_STAT_REPLAY_ROWS_STORED => replay.rows_stored,
            _ => {
                return MorkStatus::err(
                    MorkStatusCode::Internal,
                    format!("unknown indexed space statistic {stat}").into_bytes(),
                );
            }
        };
        MorkStatus::ok(value)
    })
}

/// Returns one per-query setup or traversal statistic from an indexed cursor.
///
/// Statistic ids are the `MORK_INDEXED_CURSOR_STAT_*` constants in the C
/// header. Materialized oracle cursors reject this call.
#[unsafe(no_mangle)]
pub extern "C" fn mork_query_cursor_indexed_stat(
    cursor: *const MorkQueryCursor,
    stat: u32,
) -> MorkStatus {
    with_catch_status(|| unsafe {
        let cursor = match bridge_query_cursor_ref(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        let traversal = match &cursor.source {
            BridgeQueryCursorSource::Flat(flat) => flat.stats().clone(),
            BridgeQueryCursorSource::IndexedExactResidual {
                exact, residual, ..
            } => {
                let mut stats = exact.stats().clone();
                stats.rows_emitted = stats.rows_emitted.saturating_add(residual.rows_emitted());
                stats.rows_aggregated = stats
                    .rows_aggregated
                    .saturating_add(residual.rows_aggregated());
                stats
            }
            BridgeQueryCursorSource::SemiNaive(semi_naive) => semi_naive.stats().clone(),
            BridgeQueryCursorSource::Replay { stats, .. } => stats.clone(),
            BridgeQueryCursorSource::Materialized { .. }
            | BridgeQueryCursorSource::GeneralCounted { .. } => {
                return MorkStatus::err(
                    MorkStatusCode::Internal,
                    b"indexed query statistics require an indexed cursor".to_vec(),
                );
            }
        };
        let setup = cursor
            .indexed_setup_stats
            .as_ref()
            .expect("indexed cursor must retain its setup statistics");
        let value = match stat {
            INDEXED_CURSOR_STAT_QUERY_REVISION => cursor.indexed_query_revision,
            INDEXED_CURSOR_STAT_CATALOG_BUILDS => setup.catalog_builds,
            INDEXED_CURSOR_STAT_CATALOG_ROWS_SCANNED => setup.catalog_rows_scanned,
            INDEXED_CURSOR_STAT_ACCESS_PATH_BUILDS => setup.access_path_builds,
            INDEXED_CURSOR_STAT_ACCESS_PATH_ROWS_INDEXED => setup.access_path_rows_indexed,
            INDEXED_CURSOR_STAT_PLAN_BUILDS => setup.plan_builds,
            INDEXED_CURSOR_STAT_PLAN_CACHE_HITS => setup.plan_cache_hits,
            INDEXED_CURSOR_STAT_TRIE_SEEKS => traversal.trie_seeks,
            INDEXED_CURSOR_STAT_TRIE_DESCENTS => traversal.trie_descents,
            INDEXED_CURSOR_STAT_ROWS_EMITTED => traversal.rows_emitted,
            INDEXED_CURSOR_STAT_MAX_FRAME_CELLS => {
                u64::try_from(traversal.max_frame_cells).unwrap_or(u64::MAX)
            }
            INDEXED_CURSOR_STAT_ROWS_AGGREGATED => traversal.rows_aggregated,
            INDEXED_CURSOR_STAT_REPLAY_HIT => u64::from(cursor.replay_hit),
            INDEXED_CURSOR_STAT_HAS_RESIDUAL => u64::from(cursor.indexed_has_residual),
            INDEXED_CURSOR_STAT_HAS_EXACT_PARTITION => {
                u64::from(cursor.indexed_has_exact_partition)
            }
            INDEXED_CURSOR_STAT_ROWS_AVAILABLE => u64::from(cursor.indexed_rows_available),
            _ => {
                return MorkStatus::err(
                    MorkStatusCode::Internal,
                    format!("unknown indexed cursor statistic {stat}").into_bytes(),
                );
            }
        };
        MorkStatus::ok(value)
    })
}

/// Consumes the remaining rows of an indexed cursor and returns their exact
/// bag count without reconstructing binding or expression rows.
fn encoded_multi_ref_row_multiplicity(row: &[u8], factor_count: u32) -> Result<u64, String> {
    let factor_count = usize::try_from(factor_count)
        .map_err(|_| "indexed query factor count exceeds usize".to_string())?;
    let prefix_len = factor_count
        .checked_mul(4)
        .ok_or_else(|| "indexed query factor-count prefix overflows usize".to_string())?;
    if row.len() < prefix_len {
        return Err("replayed indexed query row lacks factor multiplicities".to_string());
    }
    let mut product = 1u64;
    for chunk in row[..prefix_len].chunks_exact(4) {
        let count = u32::from_be_bytes(chunk.try_into().expect("four-byte chunk"));
        if count == 0 {
            return Err("replayed indexed query row has zero multiplicity".to_string());
        }
        product = product.checked_mul(u64::from(count)).ok_or_else(|| {
            "replayed indexed query multiplicity exceeds u64 aggregate capacity".to_string()
        })?;
    }
    Ok(product)
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_query_cursor_count_remaining(cursor: *mut MorkQueryCursor) -> MorkStatus {
    with_catch_status(|| unsafe {
        let cursor = match bridge_query_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => return err,
        };
        if cursor.pending_row.is_some() {
            return MorkStatus::err(
                MorkStatusCode::Internal,
                b"cannot aggregate an indexed cursor with a pending encoded row".to_vec(),
            );
        }
        let factor_count = match cursor.kind {
            BridgeQueryCursorKind::MultiRefV3 { factor_count } => factor_count,
            BridgeQueryCursorKind::QueryOnlyV2 => {
                return MorkStatus::err(
                    MorkStatusCode::Internal,
                    b"COUNT pushdown requires a multi-factor indexed cursor".to_vec(),
                );
            }
        };
        let pending_contextual_row = &mut cursor.pending_contextual_row;
        let count = match &mut cursor.source {
            BridgeQueryCursorSource::Flat(flat) => flat.count_remaining(),
            BridgeQueryCursorSource::IndexedExactResidual {
                exact,
                exact_done,
                residual,
                ..
            } => {
                let pending_count = pending_contextual_row
                    .as_ref()
                    .map(|row| row.multiplicity)
                    .unwrap_or(0);
                let exact_count = if *exact_done {
                    Ok(0)
                } else {
                    *exact_done = true;
                    exact.count_remaining()
                };
                exact_count
                    .and_then(|exact_count| {
                        residual.count_remaining().and_then(|residual_count| {
                            exact_count
                                .checked_add(pending_count)
                                .and_then(|count| count.checked_add(residual_count))
                                .ok_or_else(|| {
                                    "indexed exact-residual count exceeds u64 aggregate capacity"
                                        .to_string()
                                })
                        })
                    })
                    .inspect(|_| {
                        if pending_contextual_row.take().is_some() {
                            residual.rows_aggregated = residual.rows_aggregated.saturating_add(1);
                        }
                    })
            }
            BridgeQueryCursorSource::SemiNaive(semi_naive) => semi_naive.count_remaining(),
            BridgeQueryCursorSource::Replay {
                rows,
                next_row,
                stats,
            } => {
                let mut total = 0u64;
                for row in rows.iter().skip(*next_row) {
                    let multiplicity = match encoded_multi_ref_row_multiplicity(row, factor_count) {
                        Ok(multiplicity) => multiplicity,
                        Err(err) => {
                            return MorkStatus::err(MorkStatusCode::Internal, err.into_bytes());
                        }
                    };
                    total = match total.checked_add(multiplicity) {
                        Some(total) => total,
                        None => {
                            return MorkStatus::err(
                                MorkStatusCode::Internal,
                                b"replayed indexed query count exceeds u64 aggregate capacity"
                                    .to_vec(),
                            );
                        }
                    };
                    stats.rows_aggregated = stats.rows_aggregated.saturating_add(1);
                }
                *next_row = rows.len();
                Ok(total)
            }
            BridgeQueryCursorSource::Materialized { .. }
            | BridgeQueryCursorSource::GeneralCounted { .. } => {
                Err("COUNT pushdown requires an indexed cursor".to_string())
            }
        };
        match count {
            Ok(count) => MorkStatus::ok(count),
            Err(err) => MorkStatus::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_query_cursor_free(cursor: *mut MorkQueryCursor) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if !cursor.is_null() {
            drop(Box::from_raw(cursor as *mut BridgeQueryCursor));
        }
    }));
}

#[unsafe(no_mangle)]
pub extern "C" fn mork_query_cursor_next(
    cursor: *mut MorkQueryCursor,
    max_rows: u64,
    max_bytes: u64,
) -> MorkBuffer {
    with_catch_buffer(|| unsafe {
        let cursor = match bridge_query_cursor_mut(cursor) {
            Ok(cursor) => cursor,
            Err(err) => {
                return MorkBuffer::err(
                    MorkStatusCode::Null,
                    if err.message.is_null() {
                        b"null MorkQueryCursor".to_vec()
                    } else {
                        Vec::from_raw_parts(err.message, err.message_len, err.message_len)
                    },
                );
            }
        };
        match query_cursor_next_packet(cursor, max_rows, max_bytes) {
            Ok((packet, count)) => MorkBuffer::ok(packet, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
    })
}

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

/// Returns context-table query rows with exact/query-origin value refs.
#[unsafe(no_mangle)]
pub extern "C" fn mork_space_query_contextual_rows(
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
        match query_contextual_rows_packet(bridge, pattern) {
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
                bridge.expr_count = match bridge.expr_count.checked_add(count as u64) {
                    Some(next) => next,
                    None => {
                        return MorkStatus::err(
                            MorkStatusCode::Internal,
                            b"program expression count overflow".to_vec(),
                        );
                    }
                };
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
        match dump_program_chunks(&bridge.expr_chunks) {
            Ok((text, count)) => MorkBuffer::ok(text, count),
            Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
        }
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
                    Ok(count) => match checked_packet_count(count, "context dump row count") {
                        Ok(row_count) => MorkBuffer::ok(text, row_count),
                        Err(err) => MorkBuffer::err(MorkStatusCode::Internal, err.into_bytes()),
                    },
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
    use std::sync::{Mutex, MutexGuard, OnceLock};
    use std::time::{SystemTime, UNIX_EPOCH};

    static TEST_SERIAL_LOCK: OnceLock<Mutex<()>> = OnceLock::new();

    fn test_guard() -> MutexGuard<'static, ()> {
        TEST_SERIAL_LOCK
            .get_or_init(|| Mutex::new(()))
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

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

    fn read_u32_be(data: &[u8], offset: usize) -> u32 {
        u32::from_be_bytes(
            data[offset..offset + 4]
                .try_into()
                .expect("test packet has enough bytes for u32"),
        )
    }

    fn read_u64_be(data: &[u8], offset: usize) -> u64 {
        u64::from_be_bytes(
            data[offset..offset + 8]
                .try_into()
                .expect("test packet has enough bytes for u64"),
        )
    }

    fn read_u16_be(data: &[u8], offset: usize) -> u16 {
        u16::from_be_bytes(
            data[offset..offset + 2]
                .try_into()
                .expect("test packet has enough bytes for u16"),
        )
    }

    fn indexed_contextual_row_offset(data: &[u8]) -> usize {
        assert_eq!(read_u16_be(data, 4), CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION);
        let mut offset = 20usize;
        for _ in 0..read_u32_be(data, 16) {
            offset += 4;
            let entry_count = read_u32_be(data, offset);
            offset += 4;
            for _ in 0..entry_count {
                offset += 2;
                let kind = data[offset];
                offset += 2;
                match kind {
                    OPEN_VAR_REF_QUERY_SLOT => offset += 2,
                    OPEN_VAR_REF_EXACT => {
                        offset += 8;
                        let spelling_len = read_u32_be(data, offset) as usize;
                        offset += 4 + spelling_len;
                    }
                    OPEN_VAR_REF_MATCHED_EXACT => {
                        offset += 2 + 8;
                        let spelling_len = read_u32_be(data, offset) as usize;
                        offset += 4 + spelling_len;
                    }
                    OPEN_VAR_REF_MATCHED_INSTANCE => {
                        offset += 4 + 2 + 8;
                        let spelling_len = read_u32_be(data, offset) as usize;
                        offset += 4 + spelling_len;
                    }
                    _ => panic!("unknown opening-context ref kind {kind}"),
                }
            }
        }
        offset
    }

    #[test]
    fn materialized_query_rows_preserve_order_across_chunks() {
        let mut rows = MaterializedQueryRows::default();
        let row_count = MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY * 2 + 3;
        for index in 0..row_count {
            rows.push(&index.to_be_bytes());
        }

        assert_eq!(rows.len(), row_count);
        for index in [
            0,
            MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY - 1,
            MATERIALIZED_QUERY_ROW_CHUNK_CAPACITY,
            row_count - 1,
        ] {
            assert_eq!(rows.get(index), Some(index.to_be_bytes().as_slice()));
        }
        assert_eq!(rows.get(row_count), None);
    }

    #[test]
    fn add_query_remove_debug_smoke() {
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let mut scratch = Space::new();
        let edge = parse_single_expr(&mut scratch, b"(edge a b)").unwrap();
        let nested =
            parse_single_expr(&mut scratch, b"(nest (pair a (pair b c)) (text \"x y\"))").unwrap();

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
    fn normalized_expr_packet_roundtrips_long_symbols_through_counted_storage() {
        let _guard = test_guard();
        let raw = mork_space_new_pathmap();
        assert!(!raw.is_null());

        let long_symbol = vec![b'x'; 256];
        let mut packet = vec![BRIDGE_EXPR_TAG_ARITY];
        packet.extend_from_slice(&2u32.to_be_bytes());
        packet.push(BRIDGE_EXPR_TAG_SYMBOL);
        packet.extend_from_slice(&3u32.to_be_bytes());
        packet.extend_from_slice(b"doc");
        packet.push(BRIDGE_EXPR_TAG_SYMBOL);
        packet.extend_from_slice(&(long_symbol.len() as u32).to_be_bytes());
        packet.extend_from_slice(&long_symbol);

        let normalized = mork_space_normalize_expr_packet(raw, packet.as_ptr(), packet.len());
        assert!(buffer_ok(&normalized));
        let added = mork_space_add_expr_bytes(raw, normalized.data, normalized.len);
        assert!(status_ok(&added));
        mork_bytes_free(normalized.data, normalized.len);

        let dumped = mork_space_dump_expr_rows(raw);
        assert!(buffer_ok(&dumped));
        assert_eq!(dumped.count, 1);
        let bytes = unsafe { std::slice::from_raw_parts(dumped.data, dumped.len) };
        assert_eq!(
            u32::from_be_bytes(bytes[0..4].try_into().unwrap()) as usize,
            packet.len()
        );
        assert_eq!(&bytes[4..], packet.as_slice());
        mork_bytes_free(dumped.data, dumped.len);
        mork_space_free(raw);
    }

    #[test]
    fn expr_bytes_batch_mutation_matches_individual_adds() {
        let _guard = test_guard();
        let raw = mork_space_new();
        assert!(!raw.is_null());

        let mut scratch = Space::new();
        let edge = parse_single_expr(&mut scratch, b"(edge a b)").unwrap();
        let nested =
            parse_single_expr(&mut scratch, b"(nest (pair a (pair b c)) (text \"x y\"))").unwrap();

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
    fn counted_expr_bytes_batch_remove_preserves_multiplicity_and_absent_rows() {
        let _guard = test_guard();
        let raw = mork_space_new_pathmap();
        assert!(!raw.is_null());

        let mut scratch = Space::new();
        let edge = parse_single_expr(&mut scratch, b"(edge a b)").unwrap();
        let absent = parse_single_expr(&mut scratch, b"(edge x y)").unwrap();

        let mut adds = Vec::new();
        for expr in [&edge, &edge] {
            adds.extend_from_slice(&(expr.len() as u32).to_be_bytes());
            adds.extend_from_slice(expr);
        }
        let add = mork_space_add_expr_bytes_batch(raw, adds.as_ptr(), adds.len());
        assert!(status_ok(&add));
        assert_eq!(add.value, 2);

        let mut removes = Vec::new();
        for expr in [&edge, &absent, &edge, &edge] {
            removes.extend_from_slice(&(expr.len() as u32).to_be_bytes());
            removes.extend_from_slice(expr);
        }
        let remove = mork_space_remove_expr_bytes_batch(raw, removes.as_ptr(), removes.len());
        assert!(status_ok(&remove));
        assert_eq!(remove.value, 2);

        let size = mork_space_size(raw);
        assert!(status_ok(&size));
        assert_eq!(size.value, 0);
        mork_space_free(raw);
    }

    #[test]
    fn counted_expr_bytes_batch_add_rolls_back_a_mid_batch_overflow() {
        let _guard = test_guard();
        let raw = mork_space_new_pathmap();
        assert!(!raw.is_null());

        let mut scratch = Space::new();
        let first = parse_single_expr(&mut scratch, b"(edge first value)").unwrap();
        let saturated = parse_single_expr(&mut scratch, b"(edge saturated value)").unwrap();
        unsafe {
            let bridge = &mut *(raw as *mut BridgeSpace);
            assert_eq!(
                counted_insert_expr_count_cached(
                    &mut bridge.inner,
                    &saturated,
                    u32::MAX,
                    &mut bridge.counted_logical_size,
                )
                .unwrap(),
                u32::MAX
            );
        }

        let mut packet = Vec::new();
        for expr in [&first, &saturated] {
            packet.extend_from_slice(&(expr.len() as u32).to_be_bytes());
            packet.extend_from_slice(expr);
        }
        let add = mork_space_add_expr_bytes_batch(raw, packet.as_ptr(), packet.len());
        assert_eq!(add.code, MorkStatusCode::Internal as i32);
        mork_bytes_free(add.message, add.message_len);

        unsafe {
            let bridge = &mut *(raw as *mut BridgeSpace);
            assert!(!counted_contains_expr(&bridge.inner, &first).unwrap());
            assert_eq!(
                counted_exact_entry(&bridge.inner, &saturated)
                    .unwrap()
                    .unwrap()
                    .count,
                u32::MAX
            );
            assert_eq!(bridge.counted_logical_size, u64::from(u32::MAX));
        }
        mork_space_free(raw);
    }

    #[test]
    fn counted_expr_bytes_batch_remove_rolls_back_a_mid_batch_error() {
        let _guard = test_guard();
        let raw = mork_space_new_pathmap();
        assert!(!raw.is_null());

        let mut scratch = Space::new();
        let first = parse_single_expr(&mut scratch, b"(edge first value)").unwrap();
        let second = parse_single_expr(&mut scratch, b"(edge second value)").unwrap();
        unsafe {
            let bridge = &mut *(raw as *mut BridgeSpace);
            counted_insert_expr_cached(&mut bridge.inner, &first, &mut bridge.counted_logical_size)
                .unwrap();
            counted_insert_expr_cached(
                &mut bridge.inner,
                &second,
                &mut bridge.counted_logical_size,
            )
            .unwrap();
            bridge.counted_logical_size = 1;
        }

        let mut packet = Vec::new();
        for expr in [&first, &second] {
            packet.extend_from_slice(&(expr.len() as u32).to_be_bytes());
            packet.extend_from_slice(expr);
        }
        let remove = mork_space_remove_expr_bytes_batch(raw, packet.as_ptr(), packet.len());
        assert_eq!(remove.code, MorkStatusCode::Internal as i32);
        mork_bytes_free(remove.message, remove.message_len);

        unsafe {
            let bridge = &mut *(raw as *mut BridgeSpace);
            assert!(counted_contains_expr(&bridge.inner, &first).unwrap());
            assert!(counted_contains_expr(&bridge.inner, &second).unwrap());
            assert_eq!(bridge.counted_logical_size, 1);
        }
        mork_space_free(raw);
    }

    #[test]
    fn bindings_packet_starts_with_row_count() {
        let _guard = test_guard();
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
        assert!(data.len() >= 8);
        assert_eq!(read_u64_be(data, 0), 2);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn bindings_packet_uses_zero_refs_and_bridge_vars() {
        let _guard = test_guard();
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let add = mork_space_add_sexpr(raw, b"(pair a (wrap $y))".as_ptr(), 18);
        assert!(status_ok(&add));
        let packet = mork_space_query_bindings(raw, b"(pair a $x)".as_ptr(), 11);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert!(data.len() > 16);
        let row_count = read_u64_be(data, 0);
        assert_eq!(row_count, 1);
        let ref_count = read_u32_be(data, 8);
        assert_eq!(ref_count, 0);
        let binding_count = read_u32_be(data, 12);
        assert_eq!(binding_count, 1);
        let expr_len = read_u32_be(data, 18) as usize;
        let expr_text = std::str::from_utf8(&data[22..22 + expr_len]).unwrap();
        assert_eq!(expr_text, "(wrap $__mork_b1_0)");
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_packet_has_header_and_ground_value() {
        let _guard = test_guard();
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let add = mork_space_add_sexpr(raw, b"(pair a b)".as_ptr(), 10);
        assert!(status_ok(&add));

        let packet = mork_space_query_bindings_query_only_v2(raw, b"(pair a $x)".as_ptr(), 11);
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
            u16::from_be_bytes([data[6], data[7]]),
            QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY
                | QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES
                | QUERY_ONLY_V2_FLAG_WIDE_TOKENS
        );
        assert_eq!(read_u64_be(data, 8), 1);
        assert_eq!(read_u32_be(data, 16), 0);
        assert_eq!(read_u32_be(data, 20), 1);
        assert_eq!(read_u16_be(data, 24), 0);
        assert_eq!(data[26], 1);
        assert_eq!(data[27], 1);
        let expr_len = read_u32_be(data, 28) as usize;
        assert_eq!(expr_len, 6);
        assert_eq!(data[32], 0x01);
        assert_eq!(read_u32_be(data, 33), 1);
        assert_eq!(data[37], b'b');
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_supports_long_string_binding_values() {
        let _guard = test_guard();
        let raw = mork_space_new();
        assert!(!raw.is_null());
        let fact = b"(row key \"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-extra-tail\")";
        let query = b"(row key $x)";
        let add = mork_space_add_sexpr(raw, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

        let packet = mork_space_query_bindings_query_only_v2(raw, query.as_ptr(), query.len());
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(
            u16::from_be_bytes([data[6], data[7]]),
            QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY
                | QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES
                | QUERY_ONLY_V2_FLAG_WIDE_TOKENS
        );
        let expr_len = read_u32_be(data, 28) as usize;
        assert_eq!(expr_len, 80);
        assert_eq!(data[32], 0x01);
        assert_eq!(read_u32_be(data, 33), 75);
        assert_eq!(data[37], b'"');
        assert_eq!(data[111], b'"');
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_exact_match_has_zero_refs_and_zero_bindings() {
        let _guard = test_guard();
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
        assert_eq!(data.len(), 24);
        assert_eq!(
            u32::from_be_bytes([data[0], data[1], data[2], data[3]]),
            QUERY_ONLY_V2_MAGIC
        );
        assert_eq!(
            u16::from_be_bytes([data[4], data[5]]),
            QUERY_ONLY_V2_VERSION
        );
        assert_eq!(read_u64_be(data, 8), 1);
        assert_eq!(read_u32_be(data, 16), 0);
        assert_eq!(read_u32_be(data, 20), 0);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(raw);
    }

    #[test]
    fn query_only_v2_rejects_matched_side_variable_values() {
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
    fn counted_pathmap_size_tracks_logical_multiplicity() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());

        let dup = b"(dup a)";
        for expected in [1u64, 2, 3] {
            let add = mork_space_add_sexpr(counted, dup.as_ptr(), dup.len());
            assert!(status_ok(&add));
            let size = mork_space_size(counted);
            assert!(status_ok(&size));
            assert_eq!(size.value, expected);
        }

        let unique = mork_space_unique_size(counted);
        assert!(status_ok(&unique));
        assert_eq!(unique.value, 1);

        let removed = mork_space_remove_sexpr(counted, dup.as_ptr(), dup.len());
        assert!(status_ok(&removed));
        assert_eq!(removed.value, 1);
        let size = mork_space_size(counted);
        assert!(status_ok(&size));
        assert_eq!(size.value, 2);

        let cleared = mork_space_clear(counted);
        assert!(status_ok(&cleared));
        let size = mork_space_size(counted);
        assert!(status_ok(&size));
        assert_eq!(size.value, 0);

        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn counted_pathmap_contains_expr_bytes_is_alpha_invariant() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());

        let mut scratch = Space::new();
        let lhs = parse_single_expr(&mut scratch, b"(edge $x $x)").unwrap();
        let rhs = parse_single_expr(&mut scratch, b"(edge $y $y)").unwrap();
        let miss = parse_single_expr(&mut scratch, b"(edge $y $z)").unwrap();

        let add = mork_space_add_expr_bytes(counted, lhs.as_ptr(), lhs.len());
        assert!(status_ok(&add));

        let hit = mork_space_contains_expr_bytes(counted, rhs.as_ptr(), rhs.len());
        assert!(status_ok(&hit));
        assert_eq!(hit.value, 1);

        let absent = mork_space_contains_expr_bytes(counted, miss.as_ptr(), miss.len());
        assert!(status_ok(&absent));
        assert_eq!(absent.value, 0);

        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn contextual_exact_rows_report_counted_ground_multiplicity() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());

        let dup = b"(dup a)";
        for _ in 0..3 {
            let add = mork_space_add_sexpr(counted, dup.as_ptr(), dup.len());
            assert!(status_ok(&add));
        }

        let packet = mork_space_dump_contextual_exact_rows(counted);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(read_u32_be(data, 0), QUERY_ONLY_V2_MAGIC);
        assert_eq!(read_u16_be(data, 4), CONTEXTUAL_ROWS_WIRE_VERSION);
        assert_eq!(read_u16_be(data, 6), CONTEXTUAL_EXACT_ROWS_FLAGS);
        assert_eq!(read_u64_be(data, 8), 1);
        assert_eq!(read_u32_be(data, 16), 1);
        assert_eq!(read_u32_be(data, 20), 0);
        assert_eq!(read_u32_be(data, 24), 0);
        assert_eq!(read_u32_be(data, 28), 0);
        assert_eq!(read_u32_be(data, 32), 3);
        let expr_len = read_u32_be(data, 36) as usize;
        assert_eq!(data.len(), 40 + expr_len);
        assert_eq!(data[40], BRIDGE_EXPR_TAG_ARITY);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn contextual_exact_rows_reject_variable_rows_without_context() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());

        let fact = b"(edge $x $x)";
        let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

        let packet = mork_space_dump_contextual_exact_rows(counted);
        assert_eq!(packet.code, MorkStatusCode::Internal as i32);
        let text = unsafe { std::slice::from_raw_parts(packet.message, packet.message_len) };
        let rendered = std::str::from_utf8(text).unwrap();
        assert!(rendered.contains("opening context"));
        mork_bytes_free(packet.message, packet.message_len);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn counted_pathmap_query_only_v2_supports_non_ground_typed_binding_value() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());

        let fact = b"(: ax-1 (-> $a (-> $b $a)))";
        let query = b"(: $x $t)";
        let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

        let packet = mork_space_query_bindings_query_only_v2(counted, query.as_ptr(), query.len());
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
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
            QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY
                | QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES
                | QUERY_ONLY_V2_FLAG_WIDE_TOKENS
        );
        assert_eq!(read_u64_be(data, 8), 1);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn multi_ref_v3_packet_reports_direct_factor_multiplicities_and_ground_bindings() {
        let _guard = test_guard();
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
                | MULTI_REF_V3_FLAG_WIDE_TOKENS
        );
        assert_eq!(
            u32::from_be_bytes([data[8], data[9], data[10], data[11]]),
            2
        );
        assert_eq!(read_u64_be(data, 12), 1);
        assert_eq!(
            u32::from_be_bytes([data[20], data[21], data[22], data[23]]),
            2
        );
        assert_eq!(
            u32::from_be_bytes([data[24], data[25], data[26], data[27]]),
            3
        );
        assert_eq!(
            u32::from_be_bytes([data[28], data[29], data[30], data[31]]),
            3
        );
        assert_eq!(u16::from_be_bytes([data[32], data[33]]), 0);
        assert_eq!(data[34], 0);
        assert_eq!(data[35], 1);
        assert_eq!(
            u32::from_be_bytes([data[36], data[37], data[38], data[39]]) as usize,
            6
        );
        assert_eq!(data[40], 0x01);
        assert_eq!(
            u32::from_be_bytes([data[41], data[42], data[43], data[44]]),
            1
        );
        assert_eq!(data[45], b'a');
        assert_eq!(u16::from_be_bytes([data[46], data[47]]), 1);
        assert_eq!(data[49], 1);
        assert_eq!(
            u32::from_be_bytes([data[50], data[51], data[52], data[53]]) as usize,
            6
        );
        assert_eq!(data[54], 0x01);
        assert_eq!(
            u32::from_be_bytes([data[55], data[56], data[57], data[58]]),
            1
        );
        assert_eq!(data[59], b'b');
        assert_eq!(u16::from_be_bytes([data[60], data[61]]), 2);
        assert_eq!(data[63], 1);
        assert_eq!(
            u32::from_be_bytes([data[64], data[65], data[66], data[67]]) as usize,
            6
        );
        assert_eq!(data[68], 0x01);
        assert_eq!(
            u32::from_be_bytes([data[69], data[70], data[71], data[72]]),
            1
        );
        assert_eq!(data[73], b'c');
        assert_eq!(data[48], 0);
        assert_eq!(data[62], 0);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn multi_ref_v3_cursor_preserves_shared_variables_and_factor_multiplicities() {
        let _guard = test_guard();
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

        let cursor = mork_query_cursor_new_multi_ref_v3(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let packet = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(read_u32_be(data, 8), 2);
        assert_eq!(read_u64_be(data, 12), 1);
        assert_eq!(read_u32_be(data, 20), 2);
        assert_eq!(read_u32_be(data, 24), 3);
        mork_bytes_free(packet.data, packet.len);

        let exhausted = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&exhausted));
        assert_eq!(exhausted.count, 0);
        mork_query_cursor_free(cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_multi_ref_v4_is_pull_based_and_incrementally_maintained() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        for fact in [b"(edge a b)".as_slice(), b"(edge b c)", b"(edge b d)"] {
            let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
            assert!(status_ok(&add));
        }
        let query = b"(, (edge $x $y) (edge $y $z))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let catalog_builds =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_CATALOG_BUILDS);
        assert!(status_ok(&catalog_builds));
        assert_eq!(catalog_builds.value, 1);
        let catalog_rows =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_CATALOG_ROWS_SCANNED);
        assert!(status_ok(&catalog_rows));
        assert_eq!(catalog_rows.value, 3);
        let plan_builds = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_PLAN_BUILDS);
        assert!(status_ok(&plan_builds));
        assert_eq!(plan_builds.value, 1);
        let cursor_view = unsafe { bridge_query_cursor_mut(cursor).unwrap() };
        assert!(matches!(
            cursor_view.source,
            BridgeQueryCursorSource::Flat(_)
        ));
        let has_residual = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_HAS_RESIDUAL);
        assert!(status_ok(&has_residual));
        assert_eq!(has_residual.value, 0);

        let too_small = mork_query_cursor_next(cursor, 1, 21);
        assert_eq!(too_small.code, MorkStatusCode::Internal as i32);
        mork_bytes_free(too_small.message, too_small.message_len);

        let first = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&first));
        assert_eq!(first.count, 1);
        let first_data = unsafe { std::slice::from_raw_parts(first.data, first.len) };
        assert_eq!(read_u64_be(first_data, 12), 1);
        mork_bytes_free(first.data, first.len);

        let second = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&second));
        assert_eq!(second.count, 1);
        mork_bytes_free(second.data, second.len);

        let exhausted = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&exhausted));
        assert_eq!(exhausted.count, 0);
        assert_eq!(exhausted.len, 0);
        let emitted = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_EMITTED);
        assert!(status_ok(&emitted));
        assert_eq!(emitted.value, 2);
        let frame_cells =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_MAX_FRAME_CELLS);
        assert!(status_ok(&frame_cells));
        assert!(frame_cells.value > 0);
        mork_query_cursor_free(cursor);

        let builds_before =
            mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_ACCESS_PATH_BUILDS);
        assert!(status_ok(&builds_before));
        let catalog_builds =
            mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_CATALOG_BUILDS);
        assert!(status_ok(&catalog_builds));
        assert_eq!(catalog_builds.value, 1);
        let catalog_rows =
            mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_CATALOG_ROWS_SCANNED);
        assert!(status_ok(&catalog_rows));
        assert_eq!(catalog_rows.value, 3);

        let warm = mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!warm.is_null());
        let warm_builds = mork_query_cursor_indexed_stat(warm, INDEXED_CURSOR_STAT_CATALOG_BUILDS);
        assert!(status_ok(&warm_builds));
        assert_eq!(warm_builds.value, 0);
        let warm_paths =
            mork_query_cursor_indexed_stat(warm, INDEXED_CURSOR_STAT_ACCESS_PATH_BUILDS);
        assert!(status_ok(&warm_paths));
        assert_eq!(warm_paths.value, 0);
        let warm_plan_hits =
            mork_query_cursor_indexed_stat(warm, INDEXED_CURSOR_STAT_PLAN_CACHE_HITS);
        assert!(status_ok(&warm_plan_hits));
        assert_eq!(warm_plan_hits.value, 1);
        let replay_hit = mork_query_cursor_indexed_stat(warm, INDEXED_CURSOR_STAT_REPLAY_HIT);
        assert!(status_ok(&replay_hit));
        assert_eq!(replay_hit.value, 1);
        let replayed = mork_query_cursor_next(warm, 16, 4096);
        assert!(buffer_ok(&replayed));
        assert_eq!(replayed.count, 2);
        mork_bytes_free(replayed.data, replayed.len);
        let replay_eof = mork_query_cursor_next(warm, 16, 4096);
        assert!(buffer_ok(&replay_eof));
        assert_eq!(replay_eof.count, 0);
        mork_query_cursor_free(warm);
        let replay_completions =
            mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_REPLAY_COMPLETIONS);
        assert!(status_ok(&replay_completions));
        assert_eq!(replay_completions.value, 1);
        let replay_hits = mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_REPLAY_HITS);
        assert!(status_ok(&replay_hits));
        assert_eq!(replay_hits.value, 1);
        let replay_rows =
            mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_REPLAY_ROWS_STORED);
        assert!(status_ok(&replay_rows));
        assert_eq!(replay_rows.value, 2);

        let new_fact = b"(edge d e)";
        let add = mork_space_add_sexpr(counted, new_fact.as_ptr(), new_fact.len());
        assert!(status_ok(&add));
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let replay_hit = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_REPLAY_HIT);
        assert!(status_ok(&replay_hit));
        assert_eq!(
            replay_hit.value, 0,
            "a store mutation must invalidate completed query replay"
        );
        let builds_after =
            mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_ACCESS_PATH_BUILDS);
        assert!(status_ok(&builds_after));
        assert_eq!(builds_after.value, builds_before.value);
        let updates =
            mork_space_indexed_query_stat(counted, INDEXED_SPACE_STAT_INCREMENTAL_UPDATES);
        assert!(status_ok(&updates));
        assert_eq!(updates.value, 1);
        mork_query_cursor_free(cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_multi_ref_v4_time_to_first_row_does_not_materialize_the_join() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        const FACTS_PER_RELATION: usize = 512;
        for idx in 0..FACTS_PER_RELATION {
            let left = format!("(left l{idx:04} k{idx:04})");
            let right = format!("(right k{idx:04} r{idx:04})");
            for fact in [left.as_bytes(), right.as_bytes()] {
                let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
                assert!(status_ok(&add));
            }
        }

        let query = b"(, (left $x $k) (right $k $y))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let cursor_view = unsafe { bridge_query_cursor_ref(cursor).unwrap() };
        assert!(matches!(
            cursor_view.source,
            BridgeQueryCursorSource::Flat(_)
        ));

        let first = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&first));
        assert_eq!(first.count, 1);
        mork_bytes_free(first.data, first.len);

        let emitted = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_EMITTED);
        assert!(status_ok(&emitted));
        assert_eq!(
            emitted.value,
            1,
            "requesting one row must not compute the other {} results",
            FACTS_PER_RELATION - 1
        );
        let seeks = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_TRIE_SEEKS);
        assert!(status_ok(&seeks));
        assert!(
            seeks.value < 64,
            "time-to-first-row work must be bounded by index depth, not result cardinality"
        );
        let frames = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_MAX_FRAME_CELLS);
        assert!(status_ok(&frames));
        assert!(
            frames.value <= 6,
            "cursor continuation must retain bounded join state"
        );

        let second = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&second));
        assert_eq!(second.count, 1);
        mork_bytes_free(second.data, second.len);
        let emitted = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_EMITTED);
        assert!(status_ok(&emitted));
        assert_eq!(
            emitted.value, 2,
            "the cursor must resume after the first row"
        );

        mork_query_cursor_free(cursor);
        let retry =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!retry.is_null());
        let replay_hit = mork_query_cursor_indexed_stat(retry, INDEXED_CURSOR_STAT_REPLAY_HIT);
        assert!(status_ok(&replay_hit));
        assert_eq!(
            replay_hit.value, 0,
            "an abandoned cursor must not publish an incomplete replay"
        );
        mork_query_cursor_free(retry);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_multi_ref_v4_count_pushdown_skips_binding_reconstruction() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        for _ in 0..2 {
            let fact = b"(left a k)";
            let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
            assert!(status_ok(&add));
        }
        for _ in 0..3 {
            let fact = b"(right k b)";
            let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
            assert!(status_ok(&add));
        }
        let unmatched = b"(right other c)";
        let add = mork_space_add_sexpr(counted, unmatched.as_ptr(), unmatched.len());
        assert!(status_ok(&add));

        let query = b"(, (left $x $k) (right $k $y))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let count = mork_query_cursor_count_remaining(cursor);
        assert!(status_ok(&count));
        assert_eq!(count.value, 6);

        let emitted = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_EMITTED);
        assert!(status_ok(&emitted));
        assert_eq!(emitted.value, 0);
        let aggregated =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_AGGREGATED);
        assert!(status_ok(&aggregated));
        assert_eq!(aggregated.value, 1);
        let exhausted = mork_query_cursor_count_remaining(cursor);
        assert!(status_ok(&exhausted));
        assert_eq!(exhausted.value, 0);

        mork_query_cursor_free(cursor);

        let materialized = mork_query_cursor_new_multi_ref_v3(counted, query.as_ptr(), query.len());
        assert!(!materialized.is_null());
        let denied = mork_query_cursor_count_remaining(materialized);
        assert_eq!(denied.code, MorkStatusCode::Internal as i32);
        mork_bytes_free(denied.message, denied.message_len);
        mork_query_cursor_free(materialized);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_multi_ref_v4_unions_exact_and_residual_partitions_without_duplicates() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        for _ in 0..2 {
            let fact = b"(edge a ground)";
            let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
            assert!(status_ok(&add));
        }
        for _ in 0..3 {
            let fact = b"(edge $x residual)";
            let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
            assert!(status_ok(&add));
        }
        let wildcard = b"($relation a wildcard)";
        let add = mork_space_add_sexpr(counted, wildcard.as_ptr(), wildcard.len());
        assert!(status_ok(&add));

        let query = b"(, (edge a $value))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let cursor_view = unsafe { bridge_query_cursor_ref(cursor).unwrap() };
        assert!(matches!(
            cursor_view.source,
            BridgeQueryCursorSource::IndexedExactResidual { .. }
        ));
        let has_residual = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_HAS_RESIDUAL);
        assert!(status_ok(&has_residual));
        assert_eq!(has_residual.value, 1);
        let has_exact_partition =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_HAS_EXACT_PARTITION);
        assert!(status_ok(&has_exact_partition));
        assert_eq!(has_exact_partition.value, 1);

        let mut versions = Vec::new();
        let mut multiplicities = Vec::new();
        loop {
            let packet = mork_query_cursor_next(cursor, 1, 4096);
            assert!(buffer_ok(&packet));
            if packet.count == 0 {
                break;
            }
            assert_eq!(packet.count, 1);
            let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
            let version = read_u16_be(data, 4);
            versions.push(version);
            multiplicities.push(if version == MULTI_REF_V3_VERSION {
                u64::from(read_u32_be(data, 20))
            } else {
                read_u64_be(data, indexed_contextual_row_offset(data))
            });
            mork_bytes_free(packet.data, packet.len);
        }
        assert_eq!(
            versions,
            vec![
                MULTI_REF_V3_VERSION,
                CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION,
                CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION,
                CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION,
                CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION,
            ]
        );
        assert_eq!(multiplicities, vec![2, 1, 1, 1, 1]);
        let emitted = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_EMITTED);
        assert!(status_ok(&emitted));
        assert_eq!(emitted.value, 5);
        mork_query_cursor_free(cursor);

        let count_cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!count_cursor.is_null());
        let count = mork_query_cursor_count_remaining(count_cursor);
        assert!(status_ok(&count));
        assert_eq!(count.value, 6);
        let emitted =
            mork_query_cursor_indexed_stat(count_cursor, INDEXED_CURSOR_STAT_ROWS_EMITTED);
        assert!(status_ok(&emitted));
        assert_eq!(emitted.value, 0);
        let aggregated =
            mork_query_cursor_indexed_stat(count_cursor, INDEXED_CURSOR_STAT_ROWS_AGGREGATED);
        assert!(status_ok(&aggregated));
        assert_eq!(aggregated.value, 3);
        mork_query_cursor_free(count_cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_residual_packet_preserves_stored_variable_context_and_weight() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let mut scratch = Space::new();
        let expr_bytes =
            parse_single_expr(&mut scratch, b"(schema $stored (wrap $stored))").unwrap();
        let mut context = Vec::new();
        append_u32_be(&mut context, 1);
        append_u16_be(&mut context, 0);
        context.push(OPEN_VAR_REF_EXACT);
        context.push(0);
        let stored_var_id = 0x1122_3344_5566_7788u64;
        context.extend_from_slice(&stored_var_id.to_be_bytes());
        let spelling = b"stored";
        append_u32_be(&mut context, spelling.len() as u32);
        context.extend_from_slice(spelling);
        for _ in 0..2 {
            let add = mork_space_add_contextual_exact_expr_bytes(
                counted,
                expr_bytes.as_ptr(),
                expr_bytes.len(),
                context.as_ptr(),
                context.len(),
            );
            assert!(status_ok(&add));
        }

        let query = b"(, (schema $q $body))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let has_residual = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_HAS_RESIDUAL);
        assert!(status_ok(&has_residual));
        assert_eq!(has_residual.value, 1);
        let has_exact_partition =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_HAS_EXACT_PARTITION);
        assert!(status_ok(&has_exact_partition));
        assert_eq!(has_exact_partition.value, 0);
        let packet = mork_query_cursor_next(cursor, 8, 4096);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 2);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(read_u16_be(data, 4), CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION);
        assert_eq!(read_u16_be(data, 6), CONTEXTUAL_INDEXED_QUERY_ROWS_FLAGS);
        assert_eq!(read_u32_be(data, 16), 2);
        let mut offset = 20usize;
        let mut opening_groups = Vec::new();
        for expected_context_id in 0..2u32 {
            assert_eq!(read_u32_be(data, offset), expected_context_id);
            offset += 4;
            assert_eq!(read_u32_be(data, offset), 1);
            offset += 4;
            assert_eq!(read_u16_be(data, offset), 0);
            offset += 2;
            assert_eq!(data[offset], OPEN_VAR_REF_MATCHED_INSTANCE);
            assert_eq!(data[offset + 1], 0);
            offset += 2;
            opening_groups.push(read_u32_be(data, offset));
            offset += 4;
            assert_eq!(read_u16_be(data, offset), 0);
            offset += 2;
            assert_eq!(read_u64_be(data, offset), stored_var_id);
            offset += 8;
            let spelling_len = read_u32_be(data, offset) as usize;
            offset += 4;
            assert_eq!(&data[offset..offset + spelling_len], spelling);
            offset += spelling_len;
        }
        assert_ne!(opening_groups[0], opening_groups[1]);
        for _ in 0..2 {
            assert_eq!(read_u64_be(data, offset), 1);
            offset += 8;
            let binding_count = read_u32_be(data, offset);
            offset += 4;
            for _ in 0..binding_count {
                offset += 2 + 4 + 4;
                let expr_len = read_u32_be(data, offset) as usize;
                offset += 4 + expr_len;
            }
        }
        assert_eq!(offset, data.len());
        assert!(
            data.windows(8)
                .any(|bytes| bytes == stored_var_id.to_be_bytes())
        );
        assert!(data.windows(spelling.len()).any(|bytes| bytes == spelling));
        mork_bytes_free(packet.data, packet.len);
        let exhausted = mork_query_cursor_next(cursor, 8, 4096);
        assert!(buffer_ok(&exhausted));
        assert_eq!(exhausted.count, 0);
        mork_query_cursor_free(cursor);

        let partial =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!partial.is_null());
        let first = mork_query_cursor_next(partial, 1, 4096);
        assert!(buffer_ok(&first));
        assert_eq!(first.count, 1);
        mork_bytes_free(first.data, first.len);
        let remaining = mork_query_cursor_count_remaining(partial);
        assert!(status_ok(&remaining));
        assert_eq!(remaining.value, 1);
        mork_query_cursor_free(partial);

        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_context_packet_cut_rolls_back_rejected_row_interns() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let mut scratch = Space::new();
        for (source, stored_var_id) in [
            (
                b"(schema $stored first)".as_slice(),
                0x1122_3344_0000_0001u64,
            ),
            (
                b"(schema $stored second)".as_slice(),
                0x1122_3344_0000_0002u64,
            ),
        ] {
            let expr_bytes = parse_single_expr(&mut scratch, source).unwrap();
            let mut context = Vec::new();
            append_u32_be(&mut context, 1);
            append_u16_be(&mut context, 0);
            context.push(OPEN_VAR_REF_EXACT);
            context.push(0);
            context.extend_from_slice(&stored_var_id.to_be_bytes());
            append_u32_be(&mut context, 6);
            context.extend_from_slice(b"stored");
            let add = mork_space_add_contextual_exact_expr_bytes(
                counted,
                expr_bytes.as_ptr(),
                expr_bytes.len(),
                context.as_ptr(),
                context.len(),
            );
            assert!(status_ok(&add));
        }

        let query = b"(, (schema $q $body))";
        let sizing_cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!sizing_cursor.is_null());
        let sizing_packet = mork_query_cursor_next(sizing_cursor, 1, 4096);
        assert!(buffer_ok(&sizing_packet));
        assert_eq!(sizing_packet.count, 1);
        let one_row_packet_len = sizing_packet.len;
        let sizing_data =
            unsafe { std::slice::from_raw_parts(sizing_packet.data, sizing_packet.len) };
        assert_eq!(read_u32_be(sizing_data, 16), 2);
        mork_bytes_free(sizing_packet.data, sizing_packet.len);
        mork_query_cursor_free(sizing_cursor);

        let retry_cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!retry_cursor.is_null());
        let too_small = mork_query_cursor_next(
            retry_cursor,
            8,
            u64::try_from(one_row_packet_len - 1).expect("packet length fits u64"),
        );
        assert_eq!(too_small.code, MorkStatusCode::Internal as i32);
        assert_eq!(too_small.count, 0);
        assert!(too_small.data.is_null());
        mork_bytes_free(too_small.message, too_small.message_len);
        let retried = mork_query_cursor_next(retry_cursor, 1, 4096);
        assert!(buffer_ok(&retried));
        assert_eq!(retried.count, 1);
        mork_bytes_free(retried.data, retried.len);
        mork_query_cursor_free(retry_cursor);

        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let first = mork_query_cursor_next(
            cursor,
            8,
            u64::try_from(one_row_packet_len).expect("packet length fits u64"),
        );
        assert!(buffer_ok(&first));
        assert_eq!(first.count, 1);
        let first_data = unsafe { std::slice::from_raw_parts(first.data, first.len) };
        assert_eq!(read_u32_be(first_data, 16), 2);
        mork_bytes_free(first.data, first.len);

        let second = mork_query_cursor_next(cursor, 8, 4096);
        assert!(buffer_ok(&second));
        assert_eq!(second.count, 1);
        let second_data = unsafe { std::slice::from_raw_parts(second.data, second.len) };
        assert_eq!(read_u32_be(second_data, 16), 2);
        mork_bytes_free(second.data, second.len);

        let exhausted = mork_query_cursor_next(cursor, 8, 4096);
        assert!(buffer_ok(&exhausted));
        assert_eq!(exhausted.count, 0);
        mork_query_cursor_free(cursor);

        let count_cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!count_cursor.is_null());
        let first = mork_query_cursor_next(
            count_cursor,
            8,
            u64::try_from(one_row_packet_len).expect("packet length fits u64"),
        );
        assert!(buffer_ok(&first));
        assert_eq!(first.count, 1);
        mork_bytes_free(first.data, first.len);
        let remaining = mork_query_cursor_count_remaining(count_cursor);
        assert!(status_ok(&remaining));
        assert_eq!(
            remaining.value, 1,
            "COUNT after a packet cut must include the pending contextual row"
        );
        mork_query_cursor_free(count_cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn matched_instance_context_rejects_zero_opening_identity() {
        let mut exact_context = Vec::new();
        append_u32_be(&mut exact_context, 1);
        append_u16_be(&mut exact_context, 0);
        exact_context.push(OPEN_VAR_REF_EXACT);
        exact_context.push(0);
        exact_context.extend_from_slice(&0x1122_3344_5566_7788u64.to_be_bytes());
        append_u32_be(&mut exact_context, 6);
        exact_context.extend_from_slice(b"stored");

        let mut encoded = Vec::new();
        let error =
            append_matched_instance_context_entry_remapped(&mut encoded, 0, 0, &exact_context, 0)
                .unwrap_err();
        assert!(error.contains("nonzero group"));
        assert!(encoded.is_empty());
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_residual_packet_preserves_distinct_same_spelling_stored_variables() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let mut scratch = Space::new();
        let expr_bytes = parse_single_expr(
            &mut scratch,
            b"(: theorem (-> $outer (-> $middle (-> $inner $middle))))",
        )
        .unwrap();

        let stored_vars = [
            (0u16, 0x0000_0001_0000_0101u64, b"psi".as_slice()),
            (1u16, 0x0000_0001_0000_0202u64, b"phi".as_slice()),
            (2u16, 0x0000_0003_0000_0101u64, b"psi".as_slice()),
        ];
        let mut context = Vec::new();
        append_u32_be(&mut context, stored_vars.len() as u32);
        for (slot, var_id, spelling) in stored_vars {
            append_u16_be(&mut context, slot);
            context.push(OPEN_VAR_REF_EXACT);
            context.push(0);
            context.extend_from_slice(&var_id.to_be_bytes());
            append_u32_be(&mut context, spelling.len() as u32);
            context.extend_from_slice(spelling);
        }
        let add = mork_space_add_contextual_exact_expr_bytes(
            counted,
            expr_bytes.as_ptr(),
            expr_bytes.len(),
            context.as_ptr(),
            context.len(),
        );
        assert!(status_ok(&add));

        let query = b"(, (: theorem $result))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let packet = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(read_u16_be(data, 4), CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION);
        assert_eq!(read_u32_be(data, 16), 1);

        let mut offset = 20usize;
        assert_eq!(read_u32_be(data, offset), 0);
        offset += 4;
        assert_eq!(read_u32_be(data, offset), 3);
        offset += 4;
        let mut decoded = Vec::new();
        for expected_slot in 0..3u16 {
            assert_eq!(read_u16_be(data, offset), expected_slot);
            offset += 2;
            assert_eq!(data[offset], OPEN_VAR_REF_MATCHED_INSTANCE);
            assert_eq!(data[offset + 1], 0);
            offset += 2;
            let opening_group = read_u32_be(data, offset);
            offset += 4;
            assert_eq!(read_u16_be(data, offset), expected_slot);
            offset += 2;
            let var_id = read_u64_be(data, offset);
            offset += 8;
            let spelling_len = read_u32_be(data, offset) as usize;
            offset += 4;
            let spelling = data[offset..offset + spelling_len].to_vec();
            offset += spelling_len;
            decoded.push((opening_group, var_id, spelling));
        }
        assert_eq!(decoded[0].0, decoded[1].0);
        assert_eq!(decoded[1].0, decoded[2].0);
        assert_eq!(decoded[0].2, b"psi");
        assert_eq!(decoded[1].2, b"phi");
        assert_eq!(decoded[2].2, b"psi");
        assert_ne!(decoded[0].1, decoded[2].1);
        assert_eq!(decoded[0].1, 0x0000_0001_0000_0101u64);
        assert_eq!(decoded[1].1, 0x0000_0001_0000_0202u64);
        assert_eq!(decoded[2].1, 0x0000_0003_0000_0101u64);

        mork_bytes_free(packet.data, packet.len);
        mork_query_cursor_free(cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_cursor_separates_contextless_count_from_row_emission() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());

        let fact = b"(schema $stored (wrap $stored))";
        let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));
        let exact = b"(schema exact value)";
        let add = mork_space_add_sexpr(counted, exact.as_ptr(), exact.len());
        assert!(status_ok(&add));

        let query = b"(, (schema $q $body))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let rows_available =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_AVAILABLE);
        assert!(status_ok(&rows_available));
        assert_eq!(rows_available.value, 0);

        let packet = mork_query_cursor_next(cursor, 1, 4096);
        assert_eq!(packet.code, MorkStatusCode::Internal as i32);
        assert_eq!(packet.count, 0);
        assert!(packet.data.is_null());
        mork_bytes_free(packet.message, packet.message_len);

        let count = mork_query_cursor_count_remaining(cursor);
        assert!(status_ok(&count));
        assert_eq!(count.value, 2);

        mork_query_cursor_free(cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_opening_instances_survive_packet_cuts_and_separate_activations() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let mut scratch = Space::new();
        let outer = parse_single_expr(&mut scratch, b"(outer $stored)").unwrap();
        for var_id in [0x0000_0001_0000_0101u64, 0x0000_0002_0000_0101u64] {
            let mut context = Vec::new();
            append_u32_be(&mut context, 1);
            append_u16_be(&mut context, 0);
            context.push(OPEN_VAR_REF_EXACT);
            context.push(0);
            context.extend_from_slice(&var_id.to_be_bytes());
            append_u32_be(&mut context, 6);
            context.extend_from_slice(b"stored");
            let add = mork_space_add_contextual_exact_expr_bytes(
                counted,
                outer.as_ptr(),
                outer.len(),
                context.as_ptr(),
                context.len(),
            );
            assert!(status_ok(&add));
        }
        for inner in [b"(inner a)".as_slice(), b"(inner b)".as_slice()] {
            let add = mork_space_add_sexpr(counted, inner.as_ptr(), inner.len());
            assert!(status_ok(&add));
        }

        let query = b"(, (outer $x) (inner $y))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let mut opening_groups = Vec::new();
        for _ in 0..4 {
            let packet = mork_query_cursor_next(cursor, 1, 4096);
            assert!(buffer_ok(&packet));
            assert_eq!(packet.count, 1);
            let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
            assert_eq!(read_u16_be(data, 4), CONTEXTUAL_INDEXED_ROWS_WIRE_VERSION);
            let mut offset = 20usize;
            let mut packet_groups = Vec::new();
            for _ in 0..read_u32_be(data, 16) {
                offset += 4;
                let entry_count = read_u32_be(data, offset);
                offset += 4;
                for _ in 0..entry_count {
                    offset += 2;
                    let kind = data[offset];
                    offset += 2;
                    match kind {
                        OPEN_VAR_REF_QUERY_SLOT => offset += 2,
                        OPEN_VAR_REF_EXACT => {
                            offset += 8;
                            let spelling_len = read_u32_be(data, offset) as usize;
                            offset += 4 + spelling_len;
                        }
                        OPEN_VAR_REF_MATCHED_EXACT => {
                            offset += 2 + 8;
                            let spelling_len = read_u32_be(data, offset) as usize;
                            offset += 4 + spelling_len;
                        }
                        OPEN_VAR_REF_MATCHED_INSTANCE => {
                            packet_groups.push(read_u32_be(data, offset));
                            offset += 4 + 2 + 8;
                            let spelling_len = read_u32_be(data, offset) as usize;
                            offset += 4 + spelling_len;
                        }
                        _ => panic!("unknown opening-context ref kind {kind}"),
                    }
                }
            }
            assert_eq!(packet_groups.len(), 1);
            opening_groups.push(packet_groups[0]);
            mork_bytes_free(packet.data, packet.len);
        }
        assert_eq!(opening_groups[0], opening_groups[1]);
        assert_eq!(opening_groups[2], opening_groups[3]);
        assert_ne!(opening_groups[0], opening_groups[2]);
        let exhausted = mork_query_cursor_next(cursor, 1, 4096);
        assert!(buffer_ok(&exhausted));
        assert_eq!(exhausted.count, 0);

        mork_query_cursor_free(cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_multi_ref_v4_count_pushdown_includes_mixed_conjunction_products() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        for (fact, copies) in [
            (b"(left a k)".as_slice(), 2),
            (b"(left $x k)".as_slice(), 3),
            (b"(right k b)".as_slice(), 5),
        ] {
            for _ in 0..copies {
                let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
                assert!(status_ok(&add));
            }
        }
        let query = b"(, (left $x $key) (right $key $value))";
        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!cursor.is_null());
        let count = mork_query_cursor_count_remaining(cursor);
        assert!(status_ok(&count));
        assert_eq!(count.value, 25);
        mork_query_cursor_free(cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn counted_snapshot_delta_is_exact_and_rejects_non_monotone_lineage() {
        let _guard = test_guard();
        let known = mork_space_new_pathmap();
        assert!(!known.is_null());
        let edge_ab = b"(edge a b)";
        let edge_bc = b"(edge b c)";
        let edge_cd = b"(edge c d)";
        let add = mork_space_add_sexpr(known, edge_ab.as_ptr(), edge_ab.len());
        assert!(status_ok(&add));

        let old = mork_space_clone(known);
        assert!(!old.is_null());
        for _ in 0..2 {
            let add = mork_space_add_sexpr(known, edge_bc.as_ptr(), edge_bc.len());
            assert!(status_ok(&add));
        }
        let delta = mork_space_monotone_delta(known, old);
        assert!(!delta.is_null());
        let delta_size = mork_space_size(delta);
        assert!(status_ok(&delta_size));
        assert_eq!(delta_size.value, 2);
        let delta_dump = mork_space_dump(delta);
        assert!(buffer_ok(&delta_dump));
        let delta_text = unsafe {
            std::str::from_utf8_unchecked(std::slice::from_raw_parts(
                delta_dump.data,
                delta_dump.len,
            ))
        };
        assert_eq!(delta_text.matches("(edge b c)").count(), 2);
        assert!(!delta_text.contains("(edge a b)"));
        mork_bytes_free(delta_dump.data, delta_dump.len);
        let old_size = mork_space_size(old);
        assert!(status_ok(&old_size));
        assert_eq!(
            old_size.value, 1,
            "the baseline snapshot must remain immutable"
        );

        let before_removal = mork_space_clone(known);
        assert!(!before_removal.is_null());
        let removed = mork_space_remove_sexpr(known, edge_ab.as_ptr(), edge_ab.len());
        assert!(status_ok(&removed));
        assert_eq!(removed.value, 1);
        assert!(
            mork_space_monotone_delta(known, before_removal).is_null(),
            "a removal must make the descendant ineligible for monotone delta"
        );

        let after_removal = mork_space_clone(known);
        assert!(!after_removal.is_null());
        let add = mork_space_add_sexpr(known, edge_cd.as_ptr(), edge_cd.len());
        assert!(status_ok(&add));
        let post_removal_delta = mork_space_monotone_delta(known, after_removal);
        assert!(
            !post_removal_delta.is_null(),
            "a snapshot taken after a removal starts a new admissible lineage"
        );
        let post_size = mork_space_size(post_removal_delta);
        assert!(status_ok(&post_size));
        assert_eq!(post_size.value, 1);

        let unrelated = mork_space_new_pathmap();
        assert!(!unrelated.is_null());
        assert!(mork_space_monotone_delta(known, unrelated).is_null());

        mork_space_free(unrelated);
        mork_space_free(post_removal_delta);
        mork_space_free(after_removal);
        mork_space_free(before_removal);
        mork_space_free(delta);
        mork_space_free(old);
        mork_space_free(known);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_semi_naive_cursor_partitions_first_delta_and_counts_without_rows() {
        let _guard = test_guard();
        let known = mork_space_new_pathmap();
        assert!(!known.is_null());
        let old = mork_space_clone(known);
        assert!(!old.is_null());
        for fact in [b"(edge a b)".as_slice(), b"(edge b c)"] {
            let add = mork_space_add_sexpr(known, fact.as_ptr(), fact.len());
            assert!(status_ok(&add));
        }
        let delta = mork_space_monotone_delta(known, old);
        assert!(!delta.is_null());
        let query = b"(, (edge $x $y) (edge $y $z))";
        let cursor = mork_query_cursor_new_indexed_semi_naive_multi_ref_v4(
            known,
            old,
            delta,
            query.as_ptr(),
            query.len(),
        );
        assert!(!cursor.is_null());
        let count = mork_query_cursor_count_remaining(cursor);
        assert!(status_ok(&count));
        assert_eq!(
            count.value, 1,
            "a tuple with both factors in delta belongs only to its first-delta variant"
        );
        let emitted = mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_EMITTED);
        let aggregated =
            mork_query_cursor_indexed_stat(cursor, INDEXED_CURSOR_STAT_ROWS_AGGREGATED);
        assert!(status_ok(&emitted));
        assert!(status_ok(&aggregated));
        assert_eq!(emitted.value, 0);
        assert_eq!(aggregated.value, 1);

        mork_query_cursor_free(cursor);
        mork_space_free(delta);
        mork_space_free(old);
        mork_space_free(known);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_multi_ref_v4_snapshot_excludes_later_mutations() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        for fact in [b"(edge a b)".as_slice(), b"(edge b c)"] {
            let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
            assert!(status_ok(&add));
        }
        let query = b"(, (edge $x $y) (edge $y $z))";
        let old_cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!old_cursor.is_null());

        let later = b"(edge b d)";
        let add = mork_space_add_sexpr(counted, later.as_ptr(), later.len());
        assert!(status_ok(&add));
        let new_cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, query.as_ptr(), query.len());
        assert!(!new_cursor.is_null());

        let old_rows = mork_query_cursor_next(old_cursor, 16, 4096);
        assert!(buffer_ok(&old_rows));
        assert_eq!(old_rows.count, 1);
        let new_rows = mork_query_cursor_next(new_cursor, 16, 4096);
        assert!(buffer_ok(&new_rows));
        assert_eq!(new_rows.count, 2);

        mork_bytes_free(old_rows.data, old_rows.len);
        mork_bytes_free(new_rows.data, new_rows.len);
        mork_query_cursor_free(old_cursor);
        mork_query_cursor_free(new_cursor);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn indexed_multi_ref_v4_declines_shapes_requiring_general_unification() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let fact = b"(typed f (arrow a b))";
        let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));
        let nested = b"(, (typed $f (arrow $a $b)))";

        let cursor =
            mork_query_cursor_new_indexed_multi_ref_v4(counted, nested.as_ptr(), nested.len());
        assert!(cursor.is_null());
        let oracle = mork_query_cursor_new_multi_ref_v3(counted, nested.as_ptr(), nested.len());
        assert!(!oracle.is_null());
        let rows = mork_query_cursor_next(oracle, 16, 4096);
        assert!(buffer_ok(&rows));
        assert_eq!(rows.count, 1);

        mork_bytes_free(rows.data, rows.len);
        mork_query_cursor_free(oracle);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn multi_ref_v3_packet_reports_typed_annotation_bindings() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let fact = b"(: ax-1 (-> $a (-> $b $a)))";
        let query = b"(, (: $x $t))";

        let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

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
                | MULTI_REF_V3_FLAG_WIDE_TOKENS
        );
        assert_eq!(
            u32::from_be_bytes([data[8], data[9], data[10], data[11]]),
            1
        );
        assert_eq!(read_u64_be(data, 12), 1);
        assert_eq!(
            u32::from_be_bytes([data[20], data[21], data[22], data[23]]),
            1
        );
        assert_eq!(
            u32::from_be_bytes([data[24], data[25], data[26], data[27]]),
            2
        );
        assert_eq!(u16::from_be_bytes([data[28], data[29]]), 0);
        assert_eq!(data[30], 0);
        assert_eq!(data[31], 1);
        assert_eq!(
            u32::from_be_bytes([data[32], data[33], data[34], data[35]]) as usize,
            9
        );
        assert_eq!(data[36], 0x01);
        assert_eq!(
            u32::from_be_bytes([data[37], data[38], data[39], data[40]]),
            4
        );
        assert_eq!(&data[41..45], b"ax-1");
        assert_eq!(u16::from_be_bytes([data[45], data[46]]), 1);
        assert_eq!(data[47], 0);
        assert_eq!(data[48], 0);

        mork_bytes_free(packet.data, packet.len);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn contextual_query_rows_packet_reports_ground_bindings() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let fact = b"(edge a b)";
        let query = b"(edge $x $y)";
        let add = mork_space_add_sexpr(counted, fact.as_ptr(), fact.len());
        assert!(status_ok(&add));

        let packet = mork_space_query_contextual_rows(counted, query.as_ptr(), query.len());
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(
            u32::from_be_bytes([data[0], data[1], data[2], data[3]]),
            QUERY_ONLY_V2_MAGIC
        );
        assert_eq!(
            u16::from_be_bytes([data[4], data[5]]),
            CONTEXTUAL_ROWS_WIRE_VERSION
        );
        assert_eq!(
            u16::from_be_bytes([data[6], data[7]]),
            CONTEXTUAL_QUERY_ROWS_FLAGS
        );
        assert_eq!(read_u64_be(data, 8), 1);
        assert_eq!(
            u32::from_be_bytes([data[16], data[17], data[18], data[19]]),
            1
        );
        assert_eq!(
            u32::from_be_bytes([data[20], data[21], data[22], data[23]]),
            0
        );
        assert_eq!(
            u32::from_be_bytes([data[24], data[25], data[26], data[27]]),
            0
        );
        assert_eq!(
            u32::from_be_bytes([data[28], data[29], data[30], data[31]]),
            2
        );
        assert_eq!(u16::from_be_bytes([data[32], data[33]]), 0);
        assert_eq!(
            u32::from_be_bytes([data[34], data[35], data[36], data[37]]),
            0
        );
        assert_eq!(
            u32::from_be_bytes([data[38], data[39], data[40], data[41]]),
            0
        );
        assert_eq!(
            u32::from_be_bytes([data[42], data[43], data[44], data[45]]),
            6
        );
        assert_eq!(data[46], BRIDGE_EXPR_TAG_SYMBOL);
        assert_eq!(data[51], b'a');
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(counted);
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn contextual_query_value_context_uses_query_slot_refs() {
        let _guard = test_guard();
        let mut space = Space::new();
        let expr_bytes = parse_single_expr(&mut space, b"(wrap $x)").unwrap();
        let expr = Expr {
            ptr: expr_bytes.as_ptr().cast_mut(),
        };
        let binding = encode_contextual_query_binding(
            &space,
            1,
            ExprEnv::new(0, expr),
            &BTreeMap::new(),
            &[],
            None,
        )
        .unwrap();

        assert_eq!(binding.query_slot, 1);
        assert_eq!(binding.value_flags, 0);
        assert_eq!(binding.expr_packet[0], BRIDGE_EXPR_TAG_ARITY);
        assert!(binding.expr_packet.contains(&BRIDGE_EXPR_TAG_NEWVAR));
        assert_eq!(
            u32::from_be_bytes([
                binding.context[0],
                binding.context[1],
                binding.context[2],
                binding.context[3]
            ]),
            1
        );
        assert_eq!(
            u16::from_be_bytes([binding.context[4], binding.context[5]]),
            0
        );
        assert_eq!(binding.context[6], OPEN_VAR_REF_QUERY_SLOT);
        assert_eq!(binding.context[7], 0);
        assert_eq!(
            u16::from_be_bytes([binding.context[8], binding.context[9]]),
            0
        );
    }

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn contextual_query_value_context_uses_factor_scoped_stored_refs() {
        let _guard = test_guard();
        let counted = mork_space_new_pathmap();
        assert!(!counted.is_null());
        let mut scratch = Space::new();
        let expr_bytes = parse_single_expr(&mut scratch, b"(edge $stored)").unwrap();
        let mut context = Vec::new();
        append_u32_be(&mut context, 1);
        append_u16_be(&mut context, 0);
        context.push(OPEN_VAR_REF_EXACT);
        context.push(0);
        let stored_var_id = 0x0102_0304_0506_0708u64;
        context.extend_from_slice(&stored_var_id.to_be_bytes());
        let spelling = b"stored";
        append_u32_be(&mut context, spelling.len() as u32);
        context.extend_from_slice(spelling);
        let add = mork_space_add_contextual_exact_expr_bytes(
            counted,
            expr_bytes.as_ptr(),
            expr_bytes.len(),
            context.as_ptr(),
            context.len(),
        );
        assert!(status_ok(&add));

        let query = b"(edge $x)";
        let packet = mork_space_query_contextual_rows(counted, query.as_ptr(), query.len());
        assert!(buffer_ok(&packet));
        assert_eq!(packet.count, 1);
        let data = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };
        assert_eq!(read_u64_be(data, 8), 1);
        assert_eq!(read_u32_be(data, 16), 1);
        assert_eq!(read_u32_be(data, 20), 0);
        assert_eq!(read_u32_be(data, 24), 1);
        assert_eq!(read_u16_be(data, 28), 0);
        assert_eq!(data[30], OPEN_VAR_REF_MATCHED_EXACT);
        assert_eq!(data[31], 0);
        assert_eq!(data[32], 1);
        assert_eq!(data[33], 0);
        assert_eq!(
            u64::from_be_bytes(data[34..42].try_into().unwrap()),
            stored_var_id
        );
        assert_eq!(read_u32_be(data, 42), spelling.len() as u32);
        assert_eq!(&data[46..52], spelling);
        assert_eq!(read_u32_be(data, 52), 1);
        assert_eq!(read_u16_be(data, 56), 0);
        assert_eq!(read_u32_be(data, 58), 0);
        assert_eq!(read_u32_be(data, 62), 0);
        assert_eq!(read_u32_be(data, 66), 1);
        assert_eq!(data[70], BRIDGE_EXPR_TAG_NEWVAR);
        mork_bytes_free(packet.data, packet.len);
        mork_space_free(counted);
    }

    #[cfg(not(feature = "pathmap-space"))]
    #[test]
    fn multi_ref_v3_packet_reports_unavailable_without_pathmap_feature() {
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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
        let _guard = test_guard();
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

    #[cfg(feature = "pathmap-space")]
    #[test]
    fn counted_cursor_streams_million_rows_in_bounded_batches() {
        let _guard = test_guard();
        let mut bridge = Box::new(BridgeSpace {
            inner: Space::new(),
            storage_mode: BridgeStorageMode::CountedPathmap,
            counted_logical_size: 0,
            exact_contexts: HashMap::new(),
            flat_query_index: Arc::new(FlatCountedQueryIndex::default()),
            query_replay_cache: Arc::new(Mutex::new(QueryReplayCache::default())),
            query_revision: 0,
            counted_version: counted_version_root(),
        });
        let expr = parse_single_expr(&mut bridge.inner, b"(million row)").unwrap();
        counted_insert_expr_count_cached(
            &mut bridge.inner,
            &expr,
            1_000_000,
            &mut bridge.counted_logical_size,
        )
        .unwrap();

        let raw = Box::into_raw(bridge) as *mut MorkSpace;
        let cursor = mork_cursor_new(raw);
        assert!(!cursor.is_null());

        let mut total = 0u64;
        let mut batches = 0u64;
        loop {
            let packet = mork_cursor_next_expr_rows(cursor, 65_536, 1_048_576);
            assert!(buffer_ok(&packet));
            assert!(packet.count <= 65_536);
            assert!(packet.len <= 1_048_576);
            if packet.count == 0 {
                assert!(packet.data.is_null());
                break;
            }
            assert!(!packet.data.is_null());
            total += packet.count;
            batches += 1;
            mork_bytes_free(packet.data, packet.len);
        }

        assert_eq!(total, 1_000_000);
        assert!(batches > 1);
        mork_cursor_free(cursor);
        mork_space_free(raw);
    }
}
