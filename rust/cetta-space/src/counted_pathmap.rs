#[cfg(test)]
use crate::normalize_query_text;
#[cfg(test)]
use crate::parse_single_expr;
#[cfg(test)]
use crate::render_bridge_expr_text;
use crate::{
    expr_span_bytes, stable_bridge_expr_bytes, stable_bridge_expr_packet_bytes, validate_expr_bytes,
};
use mork::space::Space;
#[cfg(test)]
use mork_expr::serialize;
use mork_expr::{Expr, ExprZipper, apply};
#[cfg(feature = "pathmap-space")]
use mork_expr::{ExprEnv, Tag, byte_item, unify};
use pathmap::zipper::{
    Zipper, ZipperAbsolutePath, ZipperCreation, ZipperIteration, ZipperMoving, ZipperWriting,
};
#[cfg(feature = "pathmap-space")]
use std::collections::BTreeMap;

#[cfg(test)]
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CountedQueryRow {
    pub bindings: Vec<(u8, String)>,
    pub multiplicity: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CountedEntry {
    pub atom_expr_bytes: Vec<u8>,
    pub count: u32,
    pub full_key: Vec<u8>,
}

#[cfg(feature = "pathmap-space")]
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CountedDetailedRow {
    pub bindings: Vec<(u8, u8, bool, Vec<u8>)>,
    pub factor_counts: Vec<u32>,
}

#[cfg(feature = "pathmap-space")]
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CountedDetailedPacketRows {
    pub factor_count: u32,
    pub rows: Vec<Vec<u8>>,
}

#[derive(Debug, Clone, Copy)]
struct DecodedCountedKey<'a> {
    atom_expr_bytes: &'a [u8],
    count: u32,
}

#[cfg(feature = "pathmap-space")]
fn counted_factor_prefix(factor_expr: Expr) -> Vec<u8> {
    unsafe {
        match byte_item(*factor_expr.ptr) {
            Tag::NewVar | Tag::VarRef(_) => Vec::new(),
            _ => factor_expr
                .prefix()
                .unwrap_or_else(|full| full)
                .as_ref()
                .unwrap()
                .to_vec(),
        }
    }
}

#[cfg(test)]
fn counted_key_for_atom_with_count(atom_expr_bytes: &[u8], count: u32) -> Result<Vec<u8>, String> {
    validate_expr_bytes(atom_expr_bytes)?;
    if count == 0 {
        return Err("counted PathMap keys require a positive count".to_string());
    }
    if count == 1 {
        return Ok(atom_expr_bytes.to_vec());
    }
    let mut key = Vec::with_capacity(atom_expr_bytes.len() + std::mem::size_of::<u32>());
    key.extend_from_slice(atom_expr_bytes);
    key.extend_from_slice(&count.to_be_bytes());
    Ok(key)
}

fn decode_counted_key(key: &[u8]) -> Result<DecodedCountedKey<'_>, String> {
    if key.is_empty() {
        return Err("counted PathMap key payload is empty".to_string());
    }

    let atom_expr = Expr {
        ptr: key.as_ptr().cast_mut(),
    };
    let atom_expr_bytes = unsafe {
        atom_expr
            .span()
            .as_ref()
            .ok_or_else(|| "counted PathMap atom payload is not a valid expr".to_string())?
    };
    if atom_expr_bytes.len() == key.len() {
        return Ok(DecodedCountedKey {
            atom_expr_bytes,
            count: 1,
        });
    }
    if atom_expr_bytes.len() > key.len() {
        return Err("counted PathMap atom payload overran the key".to_string());
    }

    let count_suffix = &key[atom_expr_bytes.len()..];
    if count_suffix.len() != std::mem::size_of::<u32>() {
        return Err(format!(
            "counted PathMap key suffix must be exactly {} raw count bytes, got {}",
            std::mem::size_of::<u32>(),
            count_suffix.len()
        ));
    }
    let count = u32::from_be_bytes(
        count_suffix
            .try_into()
            .map_err(|_| "counted PathMap count suffix decode failed".to_string())?,
    );
    if count == 0 {
        return Err("counted PathMap keys must not encode zero multiplicity".to_string());
    }
    if count == 1 {
        return Err(
            "counted PathMap count suffix must not redundantly encode multiplicity one".to_string(),
        );
    }

    Ok(DecodedCountedKey {
        atom_expr_bytes,
        count,
    })
}

pub fn counted_exact_entry(
    space: &Space,
    atom_expr_bytes: &[u8],
) -> Result<Option<CountedEntry>, String> {
    let mut rz = space.btm.read_zipper_at_path(atom_expr_bytes);
    let mut found: Option<CountedEntry> = None;
    if rz.is_val() {
        found = Some(CountedEntry {
            atom_expr_bytes: atom_expr_bytes.to_vec(),
            count: 1,
            full_key: atom_expr_bytes.to_vec(),
        });
    }
    while rz.to_next_val() {
        let full_key = rz.origin_path();
        let decoded = decode_counted_key(full_key)?;
        if decoded.atom_expr_bytes != atom_expr_bytes {
            continue;
        }
        if found.is_some() {
            return Err("counted PathMap storage contains duplicate exact-atom keys".to_string());
        }
        found = Some(CountedEntry {
            atom_expr_bytes: decoded.atom_expr_bytes.to_vec(),
            count: decoded.count,
            full_key: full_key.to_vec(),
        });
    }
    Ok(found)
}

pub fn counted_contains_expr(space: &Space, atom_expr_bytes: &[u8]) -> Result<bool, String> {
    Ok(counted_exact_entry(space, atom_expr_bytes)?.is_some())
}

pub fn counted_entries(space: &Space) -> Result<Vec<CountedEntry>, String> {
    let mut rz = space.btm.read_zipper();
    let mut out = Vec::new();
    while rz.to_next_val() {
        let full_key = rz.origin_path();
        let decoded = decode_counted_key(full_key)?;
        out.push(CountedEntry {
            atom_expr_bytes: decoded.atom_expr_bytes.to_vec(),
            count: decoded.count,
            full_key: full_key.to_vec(),
        });
    }
    Ok(out)
}

fn counted_raw_count_suffix(count: u32) -> Option<[u8; std::mem::size_of::<u32>()]> {
    if count <= 1 {
        None
    } else {
        Some(count.to_be_bytes())
    }
}

fn counted_update_exact_entry(
    space: &mut Space,
    atom_expr_bytes: &[u8],
    current_count: Option<u32>,
    next_count: Option<u32>,
) -> Result<(), String> {
    let zh = space.btm.zipper_head();
    let mut wz = zh
        .write_zipper_at_exclusive_path(atom_expr_bytes)
        .map_err(|_| {
            "counted PathMap exact-key update could not acquire an exclusive writer".to_string()
        })?;

    if let Some(current_count) = current_count {
        if current_count == 1 {
            if wz.remove_val(false).is_none() {
                return Err(
                    "counted PathMap exact-key update lost the canonical multiplicity-one value"
                        .to_string(),
                );
            }
        } else {
            let current_suffix = counted_raw_count_suffix(current_count).ok_or_else(|| {
                "counted PathMap suffix update expected multiplicity > 1".to_string()
            })?;
            wz.descend_to(&current_suffix);
            if wz.remove_val(true).is_none() {
                return Err(
                    "counted PathMap exact-key update lost the canonical counted suffix"
                        .to_string(),
                );
            }
            wz.reset();
        }
    }

    if let Some(next_count) = next_count {
        if next_count == 1 {
            if wz.set_val(()).is_some() {
                return Err(
                    "counted PathMap exact-key update unexpectedly replaced an existing root value"
                        .to_string(),
                );
            }
        } else {
            let next_suffix = counted_raw_count_suffix(next_count).ok_or_else(|| {
                "counted PathMap suffix update expected multiplicity > 1".to_string()
            })?;
            wz.descend_to(&next_suffix);
            if wz.set_val(()).is_some() {
                return Err(
                    "counted PathMap exact-key update unexpectedly replaced an existing counted suffix"
                        .to_string(),
                );
            }
        }
    }

    Ok(())
}

pub fn counted_insert_expr(space: &mut Space, atom_expr_bytes: &[u8]) -> Result<u32, String> {
    validate_expr_bytes(atom_expr_bytes)?;
    let current = counted_exact_entry(space, atom_expr_bytes)?;
    let next_count = current
        .as_ref()
        .map(|entry| entry.count)
        .unwrap_or(0)
        .saturating_add(1);
    counted_update_exact_entry(
        space,
        atom_expr_bytes,
        current.as_ref().map(|entry| entry.count),
        Some(next_count),
    )?;
    Ok(next_count)
}

pub fn counted_insert_expr_batch<T: AsRef<[u8]>>(
    space: &mut Space,
    exprs: &[T],
) -> Result<u64, String> {
    let mut added = 0u64;
    for expr_bytes in exprs {
        counted_insert_expr(space, expr_bytes.as_ref())?;
        added = added.saturating_add(1);
    }
    Ok(added)
}

pub fn counted_remove_one_expr(
    space: &mut Space,
    atom_expr_bytes: &[u8],
) -> Result<Option<u32>, String> {
    validate_expr_bytes(atom_expr_bytes)?;
    let Some(current) = counted_exact_entry(space, atom_expr_bytes)? else {
        return Ok(None);
    };
    if current.count == 1 {
        counted_update_exact_entry(space, atom_expr_bytes, Some(1), None)?;
        return Ok(Some(0));
    }
    let next_count = current.count - 1;
    counted_update_exact_entry(
        space,
        atom_expr_bytes,
        Some(current.count),
        Some(next_count),
    )?;
    Ok(Some(next_count))
}

pub fn counted_remove_expr_batch<T: AsRef<[u8]>>(
    space: &mut Space,
    exprs: &[T],
) -> Result<u64, String> {
    let mut removed = 0u64;
    for expr_bytes in exprs {
        if counted_remove_one_expr(space, expr_bytes.as_ref())?.is_some() {
            removed = removed.saturating_add(1);
        }
    }
    Ok(removed)
}

pub fn counted_insert_expr_cached(
    space: &mut Space,
    atom_expr_bytes: &[u8],
    cached_logical_size: &mut u64,
) -> Result<u32, String> {
    let next_count = counted_insert_expr(space, atom_expr_bytes)?;
    *cached_logical_size = cached_logical_size.saturating_add(1);
    Ok(next_count)
}

pub fn counted_insert_expr_count_cached(
    space: &mut Space,
    atom_expr_bytes: &[u8],
    delta: u32,
    cached_logical_size: &mut u64,
) -> Result<u32, String> {
    validate_expr_bytes(atom_expr_bytes)?;
    if delta == 0 {
        return counted_exact_entry(space, atom_expr_bytes)
            .map(|entry| entry.map(|entry| entry.count).unwrap_or(0));
    }

    let current = counted_exact_entry(space, atom_expr_bytes)?;
    let current_count = current.as_ref().map(|entry| entry.count).unwrap_or(0);
    let next_count = current_count
        .checked_add(delta)
        .ok_or_else(|| "counted PathMap multiplicity overflow".to_string())?;
    counted_update_exact_entry(
        space,
        atom_expr_bytes,
        current.as_ref().map(|entry| entry.count),
        Some(next_count),
    )?;
    *cached_logical_size = cached_logical_size.saturating_add(u64::from(delta));
    Ok(next_count)
}

pub fn counted_insert_expr_batch_cached<T: AsRef<[u8]>>(
    space: &mut Space,
    exprs: &[T],
    cached_logical_size: &mut u64,
) -> Result<u64, String> {
    let added = counted_insert_expr_batch(space, exprs)?;
    *cached_logical_size = cached_logical_size.saturating_add(added);
    Ok(added)
}

pub fn counted_remove_one_expr_cached(
    space: &mut Space,
    atom_expr_bytes: &[u8],
    cached_logical_size: &mut u64,
) -> Result<Option<u32>, String> {
    let removed = counted_remove_one_expr(space, atom_expr_bytes)?;
    if removed.is_some() {
        *cached_logical_size = cached_logical_size.saturating_sub(1);
    }
    Ok(removed)
}

pub fn counted_remove_expr_batch_cached<T: AsRef<[u8]>>(
    space: &mut Space,
    exprs: &[T],
    cached_logical_size: &mut u64,
) -> Result<u64, String> {
    let removed = counted_remove_expr_batch(space, exprs)?;
    *cached_logical_size = cached_logical_size.saturating_sub(removed);
    Ok(removed)
}

pub fn counted_unique_size(space: &Space) -> u64 {
    space.btm.val_count() as u64
}

pub fn counted_logical_size(space: &Space) -> Result<u64, String> {
    let mut rz = space.btm.read_zipper();
    let mut total = 0u64;
    while rz.to_next_val() {
        total = total.saturating_add(decode_counted_key(rz.path())?.count as u64);
    }
    Ok(total)
}

pub fn counted_sync_cached_logical_size(
    space: &Space,
    cached_logical_size: &mut u64,
) -> Result<u64, String> {
    let logical_size = counted_logical_size(space)?;
    *cached_logical_size = logical_size;
    Ok(logical_size)
}

#[cfg(feature = "pathmap-space")]
fn append_u32_be(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_be_bytes());
}

#[cfg(feature = "pathmap-space")]
fn append_u16_be(out: &mut Vec<u8>, value: u16) {
    out.extend_from_slice(&value.to_be_bytes());
}

#[cfg(feature = "pathmap-space")]
const BRIDGE_VALUE_TAG_ARITY: u8 = 0x00;
#[cfg(feature = "pathmap-space")]
const BRIDGE_VALUE_TAG_VARREF: u8 = 0x03;

#[cfg(feature = "pathmap-space")]
fn encode_expr_env_packet_row(
    space: &Space,
    expr_env: ExprEnv,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
    row_vars: &mut BTreeMap<(u8, u8), u8>,
    next_row_var: &mut u8,
    resolving: &mut Vec<(u8, u8)>,
    out: &mut Vec<u8>,
) -> Result<bool, String> {
    if let Some(var) = expr_env.var_opt() {
        if let Some(rhs) = bindings.get(&var) {
            if resolving.contains(&var) {
                return Err(
                    "counted query packet materialization hit a recursive cycle".to_string()
                );
            }
            resolving.push(var);
            let is_ground = encode_expr_env_packet_row(
                space,
                *rhs,
                bindings,
                row_vars,
                next_row_var,
                resolving,
                out,
            )?;
            resolving.pop();
            return Ok(is_ground);
        }
        let packet_var = if let Some(existing) = row_vars.get(&var) {
            *existing
        } else {
            let slot = *next_row_var;
            *next_row_var = next_row_var.checked_add(1).ok_or_else(|| {
                "counted query packet row exhausted u8 variable slots".to_string()
            })?;
            row_vars.insert(var, slot);
            slot
        };
        out.push(BRIDGE_VALUE_TAG_VARREF);
        out.push(packet_var);
        return Ok(false);
    }

    let expr = expr_env.subsexpr();
    if let Some(arity) = expr.arity() {
        out.push(BRIDGE_VALUE_TAG_ARITY);
        append_u32_be(out, arity as u32);
        let mut args = Vec::new();
        expr_env.args(&mut args);
        let mut is_ground = true;
        for arg in args {
            if !encode_expr_env_packet_row(
                space,
                arg,
                bindings,
                row_vars,
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
fn append_query_only_binding_signature_packet(
    out: &mut Vec<u8>,
    bindings: &[(u8, u8, bool, Vec<u8>)],
) -> Result<(), String> {
    append_u32_be(out, bindings.len() as u32);
    for (query_slot, value_env, is_ground, expr_bytes) in bindings {
        append_u16_be(out, *query_slot as u16);
        out.push(*value_env);
        out.push(u8::from(*is_ground));
        append_u32_be(out, expr_bytes.len() as u32);
        out.extend_from_slice(expr_bytes);
    }
    Ok(())
}

#[cfg(feature = "pathmap-space")]
pub fn counted_query_only_packet_rows(
    space: &Space,
    pattern_expr_bytes: &[u8],
) -> Result<Vec<Vec<u8>>, String> {
    validate_expr_bytes(pattern_expr_bytes)?;
    let pattern_expr = Expr {
        ptr: pattern_expr_bytes.as_ptr().cast_mut(),
    };
    let n_factors = pattern_expr
        .arity()
        .ok_or_else(|| "counted query-only expected a wrapped compound expression".to_string())?
        as usize;
    if n_factors != 2 {
        return Err("counted query-only requires exactly one wrapped factor".to_string());
    }

    let mut pat_args = Vec::with_capacity(n_factors);
    ExprEnv::new(0, pattern_expr).args(&mut pat_args);
    let factor = pat_args
        .get(1)
        .ok_or_else(|| "counted query-only is missing its wrapped factor".to_string())?;

    let candidates = counted_factor_candidates(space, factor.subsexpr())?;
    if candidates.is_empty() {
        return Ok(Vec::new());
    }

    let mut rows = Vec::new();
    for candidate in candidates {
        let atom_expr = Expr {
            ptr: candidate.atom_expr_bytes.as_ptr().cast_mut(),
        };
        if let Ok(bindings) = unify(vec![(*factor, ExprEnv::new(1, atom_expr))]) {
            let signature = query_binding_signature_packet(space, &bindings)?;
            let mut row = Vec::new();
            append_u32_be(&mut row, 0);
            append_query_only_binding_signature_packet(&mut row, &signature)?;
            for _ in 0..candidate.count {
                rows.push(row.clone());
            }
        }
    }

    Ok(rows)
}

#[cfg(feature = "pathmap-space")]
fn append_multi_ref_counted_multiplicities_packet(
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

fn append_expr_row_packet(out: &mut Vec<u8>, expr_bytes: &[u8]) -> Result<(), String> {
    let len = u32::try_from(expr_bytes.len())
        .map_err(|_| "expr row exceeds u32 packet length".to_string())?;
    out.extend_from_slice(&len.to_be_bytes());
    out.extend_from_slice(expr_bytes);
    Ok(())
}

pub fn counted_expr_row_packet(space: &Space) -> Result<(Vec<u8>, u32), String> {
    let mut packet = Vec::new();
    let mut count = 0u32;

    for entry in counted_entries(space)? {
        for _ in 0..entry.count {
            let expr = Expr {
                ptr: entry.atom_expr_bytes.as_ptr().cast_mut(),
            };
            let encoded = stable_bridge_expr_packet_bytes(space, expr)?;
            append_expr_row_packet(&mut packet, &encoded)?;
            count = count.saturating_add(1);
        }
    }

    Ok((packet, count))
}

pub fn counted_sexpr_text(space: &Space) -> Result<(Vec<u8>, u32), String> {
    let mut text = Vec::new();
    let mut count = 0u32;

    for entry in counted_entries(space)? {
        let mut line = Vec::new();
        let mut view = Space::new();
        view.btm.insert(&entry.atom_expr_bytes, ());
        view.dump_all_sexpr(&mut line)?;
        for _ in 0..entry.count {
            text.extend_from_slice(&line);
            count = count.saturating_add(1);
        }
    }

    Ok((text, count))
}

#[cfg(feature = "pathmap-space")]
pub fn counted_factor_candidates(
    space: &Space,
    factor_expr: Expr,
) -> Result<Vec<CountedEntry>, String> {
    let prefix = counted_factor_prefix(factor_expr);
    let mut rz = space.btm.read_zipper_at_path(&prefix);
    let mut out = Vec::new();
    if rz.is_val() {
        let full_key = rz.origin_path();
        let decoded = decode_counted_key(full_key)?;
        let atom_expr = Expr {
            ptr: decoded.atom_expr_bytes.as_ptr().cast_mut(),
        };
        if unify(vec![(
            ExprEnv::new(0, factor_expr),
            ExprEnv::new(1, atom_expr),
        )])
        .is_ok()
        {
            out.push(CountedEntry {
                atom_expr_bytes: decoded.atom_expr_bytes.to_vec(),
                count: decoded.count,
                full_key: full_key.to_vec(),
            });
        }
    }
    while rz.to_next_val() {
        let full_key = rz.origin_path();
        let decoded = decode_counted_key(full_key)?;
        let atom_expr = Expr {
            ptr: decoded.atom_expr_bytes.as_ptr().cast_mut(),
        };
        if unify(vec![(
            ExprEnv::new(0, factor_expr),
            ExprEnv::new(1, atom_expr),
        )])
        .is_ok()
        {
            out.push(CountedEntry {
                atom_expr_bytes: decoded.atom_expr_bytes.to_vec(),
                count: decoded.count,
                full_key: full_key.to_vec(),
            });
        }
    }
    Ok(out)
}

#[cfg(feature = "pathmap-space")]
fn materialized_expr_env_size(
    expr_env: ExprEnv,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
    stack: &mut Vec<(u8, u8)>,
) -> Result<usize, String> {
    if let Some(var) = expr_env.var_opt() {
        if let Some(rhs) = bindings.get(&var) {
            if stack.contains(&var) {
                return Err(
                    "counted query binding materialization hit a recursive cycle".to_string(),
                );
            }
            stack.push(var);
            let size = materialized_expr_env_size(*rhs, bindings, stack)?;
            stack.pop();
            return Ok(size);
        }
        return Ok(1);
    }

    let expr = expr_env.subsexpr();
    if expr.arity().is_none() {
        return Ok(expr_span_bytes(expr).len());
    }

    let mut args = Vec::new();
    expr_env.args(&mut args);
    let mut total = 1usize;
    for arg in args {
        total = total.saturating_add(materialized_expr_env_size(arg, bindings, stack)?);
    }
    Ok(total)
}

#[cfg(feature = "pathmap-space")]
fn materialize_expr_env_bytes(
    expr_env: ExprEnv,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
) -> Result<Vec<u8>, String> {
    let mut stack = Vec::new();
    let cap = materialized_expr_env_size(expr_env, bindings, &mut stack)?.saturating_add(64);
    let mut out = vec![0u8; cap];
    let mut ez = ExprZipper::new(expr_env.subsexpr());
    let mut oz = ExprZipper::new(Expr {
        ptr: out.as_mut_ptr(),
    });
    let mut cycled = BTreeMap::<(u8, u8), u8>::new();
    let mut apply_stack = Vec::<(u8, u8)>::new();
    let mut assignments = Vec::<(u8, u8)>::new();
    let _ = apply(
        expr_env.n,
        expr_env.v,
        0,
        &mut ez,
        bindings,
        &mut oz,
        &mut cycled,
        &mut apply_stack,
        &mut assignments,
    );
    out.truncate(oz.loc);
    validate_expr_bytes(&out)?;
    Ok(out)
}

#[cfg(feature = "pathmap-space")]
fn query_binding_signature(
    space: &Space,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
) -> Result<Vec<(u8, u8, bool, Vec<u8>)>, String> {
    bindings
        .iter()
        .filter_map(|(&(side, idx), expr_env)| {
            if side != 0 {
                return None;
            }
            Some(
                materialize_expr_env_bytes(*expr_env, bindings).and_then(|expr_bytes| {
                    let raw_expr = Expr {
                        ptr: expr_bytes.as_ptr().cast_mut(),
                    };
                    let bridge_expr = stable_bridge_expr_bytes(space, raw_expr)?;
                    let is_ground = Expr {
                        ptr: bridge_expr.as_ptr().cast_mut(),
                    }
                    .is_ground();
                    Ok((idx, expr_env.n, is_ground, bridge_expr))
                }),
            )
        })
        .collect()
}

#[cfg(feature = "pathmap-space")]
fn query_binding_signature_packet(
    space: &Space,
    bindings: &BTreeMap<(u8, u8), ExprEnv>,
) -> Result<Vec<(u8, u8, bool, Vec<u8>)>, String> {
    let mut row_vars = BTreeMap::<(u8, u8), u8>::new();
    let mut next_row_var = 0u8;
    let mut out = Vec::new();
    for (&(side, idx), expr_env) in bindings.iter() {
        if side != 0 {
            continue;
        }
        let mut encoded = Vec::new();
        let mut resolving = Vec::new();
        let is_ground = encode_expr_env_packet_row(
            space,
            *expr_env,
            bindings,
            &mut row_vars,
            &mut next_row_var,
            &mut resolving,
            &mut encoded,
        )?;
        out.push((idx, 0u8, is_ground, encoded));
    }
    Ok(out)
}

#[cfg(feature = "pathmap-space")]
fn accumulate_counted_query_rows_detailed(
    space: &Space,
    factors: &[ExprEnv],
    candidate_lists: &[Vec<CountedEntry>],
    depth: usize,
    chosen: &mut Vec<usize>,
    rows: &mut Vec<CountedDetailedRow>,
) {
    if depth == factors.len() {
        let mut stack = Vec::with_capacity(factors.len());
        let mut factor_counts = Vec::with_capacity(factors.len());
        for (factor_idx, factor) in factors.iter().enumerate() {
            let chosen_entry = &candidate_lists[factor_idx][chosen[factor_idx]];
            let atom_expr = Expr {
                ptr: chosen_entry.atom_expr_bytes.as_ptr().cast_mut(),
            };
            stack.push((*factor, ExprEnv::new((factor_idx + 1) as u8, atom_expr)));
            factor_counts.push(chosen_entry.count);
        }
        if let Ok(bindings) = unify(stack) {
            if let Ok(signature) = query_binding_signature(space, &bindings) {
                rows.push(CountedDetailedRow {
                    bindings: signature,
                    factor_counts,
                });
            }
        }
        return;
    }

    for idx in 0..candidate_lists[depth].len() {
        chosen.push(idx);
        accumulate_counted_query_rows_detailed(
            space,
            factors,
            candidate_lists,
            depth + 1,
            chosen,
            rows,
        );
        chosen.pop();
    }
}

#[cfg(feature = "pathmap-space")]
pub fn counted_query_rows_detailed(
    space: &Space,
    pattern_expr_bytes: &[u8],
) -> Result<Vec<CountedDetailedRow>, String> {
    validate_expr_bytes(pattern_expr_bytes)?;
    let pattern_expr = Expr {
        ptr: pattern_expr_bytes.as_ptr().cast_mut(),
    };
    let n_factors = pattern_expr
        .arity()
        .ok_or_else(|| "counted query expected a wrapped compound expression".to_string())?
        as usize;
    if n_factors < 2 {
        return Err("counted query requires at least one wrapped factor".to_string());
    }

    let mut pat_args = Vec::with_capacity(n_factors);
    ExprEnv::new(0, pattern_expr).args(&mut pat_args);
    let factors = &pat_args[1..];
    if factors.is_empty() {
        return Err("counted query is missing wrapped factors".to_string());
    }

    let mut candidate_lists = Vec::with_capacity(factors.len());
    for factor in factors {
        let candidates = counted_factor_candidates(space, factor.subsexpr())?;
        if candidates.is_empty() {
            return Ok(Vec::new());
        }
        candidate_lists.push(candidates);
    }

    let mut rows = Vec::<CountedDetailedRow>::new();
    let mut chosen = Vec::with_capacity(factors.len());
    accumulate_counted_query_rows_detailed(
        space,
        factors,
        &candidate_lists,
        0,
        &mut chosen,
        &mut rows,
    );

    Ok(rows)
}

#[cfg(feature = "pathmap-space")]
fn accumulate_counted_query_packet_rows(
    space: &Space,
    factors: &[ExprEnv],
    candidate_lists: &[Vec<CountedEntry>],
    depth: usize,
    chosen: &mut Vec<usize>,
    rows: &mut Vec<Vec<u8>>,
) -> Result<(), String> {
    if depth == factors.len() {
        let mut stack = Vec::with_capacity(factors.len());
        let mut factor_counts = Vec::with_capacity(factors.len());
        for (factor_idx, factor) in factors.iter().enumerate() {
            let chosen_entry = &candidate_lists[factor_idx][chosen[factor_idx]];
            let atom_expr = Expr {
                ptr: chosen_entry.atom_expr_bytes.as_ptr().cast_mut(),
            };
            stack.push((*factor, ExprEnv::new((factor_idx + 1) as u8, atom_expr)));
            factor_counts.push(chosen_entry.count);
        }
        if let Ok(bindings) = unify(stack) {
            let signature = query_binding_signature_packet(space, &bindings)?;
            let mut row = Vec::new();
            append_multi_ref_counted_multiplicities_packet(&mut row, &factor_counts)
                .and_then(|()| append_query_only_binding_signature_packet(&mut row, &signature))?;
            rows.push(row);
        }
        return Ok(());
    }

    for idx in 0..candidate_lists[depth].len() {
        chosen.push(idx);
        accumulate_counted_query_packet_rows(
            space,
            factors,
            candidate_lists,
            depth + 1,
            chosen,
            rows,
        )?;
        chosen.pop();
    }
    Ok(())
}

#[cfg(feature = "pathmap-space")]
pub fn counted_query_rows_detailed_packet_rows(
    space: &Space,
    pattern_expr_bytes: &[u8],
) -> Result<CountedDetailedPacketRows, String> {
    validate_expr_bytes(pattern_expr_bytes)?;
    let pattern_expr = Expr {
        ptr: pattern_expr_bytes.as_ptr().cast_mut(),
    };
    let factor_count = pattern_expr
        .arity()
        .ok_or_else(|| "counted multi-ref packet expected a wrapped query".to_string())?
        .checked_sub(1)
        .ok_or_else(|| "counted multi-ref packet expected a wrapped query".to_string())?;
    if factor_count == 0 {
        return Err("counted multi-ref packet requires at least one query factor".to_string());
    }

    let mut pat_args = Vec::with_capacity((factor_count as usize) + 1);
    ExprEnv::new(0, pattern_expr).args(&mut pat_args);
    let factors = &pat_args[1..];

    let mut candidate_lists = Vec::with_capacity(factors.len());
    for factor in factors {
        let candidates = counted_factor_candidates(space, factor.subsexpr())?;
        if candidates.is_empty() {
            return Ok(CountedDetailedPacketRows {
                factor_count: factor_count as u32,
                rows: Vec::new(),
            });
        }
        candidate_lists.push(candidates);
    }

    let mut rows = Vec::new();
    let mut chosen = Vec::with_capacity(factors.len());
    accumulate_counted_query_packet_rows(
        space,
        factors,
        &candidate_lists,
        0,
        &mut chosen,
        &mut rows,
    )?;

    Ok(CountedDetailedPacketRows {
        factor_count: factor_count as u32,
        rows,
    })
}

#[cfg(test)]
pub fn counted_query_rows(
    space: &Space,
    pattern_expr_bytes: &[u8],
) -> Result<Vec<CountedQueryRow>, String> {
    let detailed_rows = counted_query_rows_detailed(space, pattern_expr_bytes)?;
    let mut aggregated = BTreeMap::<Vec<(u8, String)>, u64>::new();
    for row in detailed_rows {
        let multiplicity = row
            .factor_counts
            .iter()
            .fold(1u64, |acc, count| acc.saturating_mul(*count as u64));
        let bindings = row
            .bindings
            .into_iter()
            .map(|(slot, env, _ground, value)| {
                (
                    slot,
                    render_bridge_expr_text(&value, env).unwrap_or_else(|_| serialize(&value)),
                )
            })
            .collect::<Vec<_>>();
        let slot = aggregated.entry(bindings).or_insert(0);
        *slot = slot.saturating_add(multiplicity);
    }
    Ok(aggregated
        .into_iter()
        .map(|(bindings, multiplicity)| CountedQueryRow {
            bindings,
            multiplicity,
        })
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse_expr(space: &mut Space, text: &str) -> Vec<u8> {
        parse_single_expr(space, text.as_bytes()).expect("expression should parse")
    }

    #[test]
    fn counted_key_roundtrip_keeps_atom_prefix_and_count_expr() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(edge a b)");
        let key = counted_key_for_atom_with_count(&atom, 3).expect("key should encode");
        let decoded = decode_counted_key(&key).expect("key should decode");
        let count_bytes = &key[decoded.atom_expr_bytes.len()..];

        assert!(key.starts_with(atom.as_slice()));
        assert_eq!(decoded.atom_expr_bytes, atom.as_slice());
        assert_eq!(count_bytes, 3u32.to_be_bytes().as_slice());
        assert_eq!(decoded.count, 3);
    }

    #[test]
    fn counted_key_for_one_is_plain_atom_bytes() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(edge a b)");
        let key = counted_key_for_atom_with_count(&atom, 1).expect("key should encode");
        let decoded = decode_counted_key(&key).expect("key should decode");
        let count_expr_bytes = &key[decoded.atom_expr_bytes.len()..];

        assert_eq!(key, atom);
        assert_eq!(decoded.atom_expr_bytes, atom.as_slice());
        assert!(count_expr_bytes.is_empty());
        assert_eq!(decoded.count, 1);
    }

    #[test]
    fn counted_key_rejects_non_canonical_suffix_length() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(edge a b)");
        let mut malformed = atom.clone();
        malformed.extend_from_slice(&[0x00, 0x02]);
        let err = decode_counted_key(&malformed).expect_err("malformed suffix should fail");

        assert!(err.contains("exactly 4 raw count bytes"));
    }

    #[test]
    fn ordinary_pair_expr_is_not_atom_prefix_preserving() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(edge a b)");
        let pair = parse_expr(&mut space, "((edge a b) 3)");
        let atom_expr = Expr {
            ptr: atom.as_ptr().cast_mut(),
        };
        let atom_prefix = unsafe {
            atom_expr
                .prefix()
                .unwrap_or_else(|full| full)
                .as_ref()
                .unwrap()
        };

        assert!(!pair.starts_with(atom_prefix));
    }

    #[test]
    fn counted_insert_and_remove_one_use_single_trie_truth() {
        let mut space = Space::new();
        let dup_a = parse_expr(&mut space, "(dup a)");
        let dup_b = parse_expr(&mut space, "(dup b)");

        assert_eq!(counted_insert_expr(&mut space, &dup_a).unwrap(), 1);
        assert_eq!(counted_insert_expr(&mut space, &dup_a).unwrap(), 2);
        assert_eq!(counted_insert_expr(&mut space, &dup_b).unwrap(), 1);

        assert_eq!(counted_unique_size(&space), 2);
        assert_eq!(counted_logical_size(&space).unwrap(), 3);

        assert_eq!(
            counted_remove_one_expr(&mut space, &dup_a).unwrap(),
            Some(1)
        );
        assert_eq!(counted_unique_size(&space), 2);
        assert_eq!(counted_logical_size(&space).unwrap(), 2);

        assert_eq!(
            counted_remove_one_expr(&mut space, &dup_a).unwrap(),
            Some(0)
        );
        assert_eq!(counted_unique_size(&space), 1);
        assert_eq!(counted_logical_size(&space).unwrap(), 1);
    }

    #[test]
    fn counted_duplicate_updates_keep_one_canonical_exact_entry() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(dup a)");

        assert_eq!(counted_insert_expr(&mut space, &atom).unwrap(), 1);
        let entries = counted_entries(&space).unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].count, 1);
        assert_eq!(entries[0].full_key, atom);

        assert_eq!(counted_insert_expr(&mut space, &atom).unwrap(), 2);
        let entries = counted_entries(&space).unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].count, 2);
        assert_eq!(
            entries[0].full_key,
            [atom.as_slice(), 2u32.to_be_bytes().as_slice()].concat()
        );

        assert_eq!(counted_insert_expr(&mut space, &atom).unwrap(), 3);
        let entries = counted_entries(&space).unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].count, 3);
        assert_eq!(
            entries[0].full_key,
            [atom.as_slice(), 3u32.to_be_bytes().as_slice()].concat()
        );

        assert_eq!(counted_remove_one_expr(&mut space, &atom).unwrap(), Some(2));
        let entries = counted_entries(&space).unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].count, 2);

        assert_eq!(counted_remove_one_expr(&mut space, &atom).unwrap(), Some(1));
        let entries = counted_entries(&space).unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].count, 1);
        assert_eq!(entries[0].full_key, atom);
    }

    #[test]
    fn counted_contains_expr_is_alpha_invariant_over_bridge_bytes() {
        let mut space = Space::new();
        let lhs = parse_expr(&mut space, "(edge $x $x)");
        let rhs = parse_expr(&mut space, "(edge $y $y)");
        let miss = parse_expr(&mut space, "(edge $y $z)");

        assert_eq!(lhs, rhs);
        assert_ne!(lhs, miss);
        assert!(!counted_contains_expr(&space, &lhs).unwrap());

        counted_insert_expr(&mut space, &lhs).unwrap();

        assert!(counted_contains_expr(&space, &lhs).unwrap());
        assert!(counted_contains_expr(&space, &rhs).unwrap());
        assert!(!counted_contains_expr(&space, &miss).unwrap());
    }

    #[test]
    fn counted_batch_updates_report_logical_delta_and_keep_canonical_entries() {
        let mut space = Space::new();
        let dup_a = parse_expr(&mut space, "(dup a)");
        let dup_b = parse_expr(&mut space, "(dup b)");
        let batch = vec![dup_a.clone(), dup_a.clone(), dup_b.clone()];

        assert_eq!(counted_insert_expr_batch(&mut space, &batch).unwrap(), 3);
        assert_eq!(counted_logical_size(&space).unwrap(), 3);
        let entries = counted_entries(&space).unwrap();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].count, 2);
        assert_eq!(entries[1].count, 1);

        let removals = vec![dup_a.clone(), dup_b.clone(), dup_b.clone()];
        assert_eq!(counted_remove_expr_batch(&mut space, &removals).unwrap(), 2);
        assert_eq!(counted_logical_size(&space).unwrap(), 1);
        let entries = counted_entries(&space).unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].atom_expr_bytes, dup_a);
        assert_eq!(entries[0].count, 1);
    }

    #[test]
    fn counted_cached_logical_size_helpers_track_single_and_batch_updates() {
        let mut space = Space::new();
        let dup_a = parse_expr(&mut space, "(dup a)");
        let dup_b = parse_expr(&mut space, "(dup b)");
        let mut cached_logical_size = 0u64;

        assert_eq!(
            counted_sync_cached_logical_size(&space, &mut cached_logical_size).unwrap(),
            0
        );
        assert_eq!(cached_logical_size, 0);

        assert_eq!(
            counted_insert_expr_cached(&mut space, &dup_a, &mut cached_logical_size).unwrap(),
            1
        );
        assert_eq!(cached_logical_size, 1);

        let batch = vec![dup_a.clone(), dup_b.clone()];
        assert_eq!(
            counted_insert_expr_batch_cached(&mut space, &batch, &mut cached_logical_size).unwrap(),
            2
        );
        assert_eq!(cached_logical_size, 3);

        assert_eq!(
            counted_remove_one_expr_cached(&mut space, &dup_a, &mut cached_logical_size).unwrap(),
            Some(1)
        );
        assert_eq!(cached_logical_size, 2);

        assert_eq!(
            counted_remove_expr_batch_cached(&mut space, &batch, &mut cached_logical_size).unwrap(),
            2
        );
        assert_eq!(cached_logical_size, 0);
        assert_eq!(
            counted_sync_cached_logical_size(&space, &mut cached_logical_size).unwrap(),
            0
        );
        assert_eq!(cached_logical_size, 0);
    }

    #[test]
    fn counted_expr_row_packet_repeats_rows_by_multiplicity() {
        let mut space = Space::new();
        let dup_a = parse_expr(&mut space, "(dup a)");
        let dup_b = parse_expr(&mut space, "(dup b)");
        counted_insert_expr(&mut space, &dup_a).unwrap();
        counted_insert_expr(&mut space, &dup_a).unwrap();
        counted_insert_expr(&mut space, &dup_b).unwrap();

        let (packet, rows) = counted_expr_row_packet(&space).unwrap();
        assert_eq!(rows, 3);

        let mut offset = 0usize;
        let mut decoded = Vec::new();
        while offset < packet.len() {
            let len = u32::from_be_bytes([
                packet[offset],
                packet[offset + 1],
                packet[offset + 2],
                packet[offset + 3],
            ]) as usize;
            offset += 4;
            decoded.push(packet[offset..offset + len].to_vec());
            offset += len;
        }

        assert_eq!(decoded.len(), 3);
        let dup_a_packet = stable_bridge_expr_packet_bytes(
            &space,
            Expr {
                ptr: dup_a.as_ptr().cast_mut(),
            },
        )
        .unwrap();
        let dup_b_packet = stable_bridge_expr_packet_bytes(
            &space,
            Expr {
                ptr: dup_b.as_ptr().cast_mut(),
            },
        )
        .unwrap();
        assert_eq!(decoded[0], dup_a_packet);
        assert_eq!(decoded[1], dup_a_packet);
        assert_eq!(decoded[2], dup_b_packet);
    }

    #[test]
    fn counted_sexpr_text_repeats_lines_by_multiplicity() {
        let mut space = Space::new();
        let dup_a = parse_expr(&mut space, "(dup a)");
        let dup_b = parse_expr(&mut space, "(dup b)");
        counted_insert_expr(&mut space, &dup_a).unwrap();
        counted_insert_expr(&mut space, &dup_a).unwrap();
        counted_insert_expr(&mut space, &dup_b).unwrap();

        let (text, rows) = counted_sexpr_text(&space).unwrap();
        let rendered = String::from_utf8(text).unwrap();

        assert_eq!(rows, 3);
        assert_eq!(rendered, "(dup a)\n(dup a)\n(dup b)\n");
    }

    #[test]
    fn counted_single_factor_query_returns_logical_multiplicity() {
        let mut space = Space::new();
        let dup_a = parse_expr(&mut space, "(dup a)");
        let dup_b = parse_expr(&mut space, "(dup b)");
        counted_insert_expr(&mut space, &dup_a).unwrap();
        counted_insert_expr(&mut space, &dup_a).unwrap();
        counted_insert_expr(&mut space, &dup_b).unwrap();

        let query = normalize_query_text(b"(dup $x)").unwrap();
        let query_expr = parse_single_expr(&mut space, &query).unwrap();
        let rows = counted_query_rows(&space, &query_expr).unwrap();

        assert_eq!(
            rows,
            vec![
                CountedQueryRow {
                    bindings: vec![(0, "a".to_string())],
                    multiplicity: 2,
                },
                CountedQueryRow {
                    bindings: vec![(0, "b".to_string())],
                    multiplicity: 1,
                },
            ]
        );
    }

    #[test]
    fn counted_conjunction_query_multiplies_factor_counts() {
        let mut space = Space::new();
        let edge_ab = parse_expr(&mut space, "(edge a b)");
        let edge_bc = parse_expr(&mut space, "(edge b c)");

        counted_insert_expr(&mut space, &edge_ab).unwrap();
        counted_insert_expr(&mut space, &edge_ab).unwrap();
        counted_insert_expr(&mut space, &edge_bc).unwrap();
        counted_insert_expr(&mut space, &edge_bc).unwrap();
        counted_insert_expr(&mut space, &edge_bc).unwrap();

        let query = normalize_query_text(b"(edge $x $y) (edge $y $z)").unwrap();
        let query_expr = parse_single_expr(&mut space, &query).unwrap();
        let rows = counted_query_rows(&space, &query_expr).unwrap();

        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].multiplicity, 6);
        assert_eq!(
            rows[0]
                .bindings
                .iter()
                .map(|(_, value)| value.clone())
                .collect::<Vec<_>>(),
            vec!["a".to_string(), "b".to_string(), "c".to_string()]
        );
    }

    #[test]
    fn counted_typed_annotation_query_matches_single_fact() {
        let mut space = Space::new();
        let typed = parse_expr(&mut space, "(: ax-1 (→ $a (→ $b $a)))");
        counted_insert_expr(&mut space, &typed).unwrap();

        let query = normalize_query_text(b"(: $x $a)").unwrap();
        let query_expr = parse_single_expr(&mut space, &query).unwrap();
        let rows = counted_query_rows(&space, &query_expr).unwrap();

        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].multiplicity, 1);
        assert_eq!(rows[0].bindings[0], (0, "ax-1".to_string()));
    }

    #[test]
    fn counted_rule_query_materializes_non_ground_result_value() {
        let mut space = Space::new();
        let fact = parse_expr(&mut space, "(: ax-mp (-> (→ $p $q) $p $q))");
        counted_insert_expr(&mut space, &fact).unwrap();

        let query = normalize_query_text(
            "(: $f (-> (→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r))) (→ $p (→ $q $r)) $b))"
                .as_bytes(),
        )
        .unwrap();
        let query_expr = parse_single_expr(&mut space, &query).unwrap();
        let rows = counted_query_rows(&space, &query_expr).unwrap();

        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].multiplicity, 1);
        assert!(rows[0].bindings.iter().any(|(_, value)| value == "ax-mp"));
        assert!(rows[0].bindings.iter().any(|(_, value)| {
            value == "(→ (→ $__mork_b1_1 $__mork_b1_2) (→ $__mork_b1_1 $__mork_b1_3))"
        }));
    }

    #[test]
    fn counted_query_packet_rows_keep_distinct_rhs_variables_across_bindings() {
        fn read_u32(packet: &[u8], off: &mut usize) -> u32 {
            let value = u32::from_be_bytes(packet[*off..*off + 4].try_into().unwrap());
            *off += 4;
            value
        }

        fn read_u16(packet: &[u8], off: &mut usize) -> u16 {
            let value = u16::from_be_bytes(packet[*off..*off + 2].try_into().unwrap());
            *off += 2;
            value
        }

        let mut space = Space::new();
        let fact = parse_expr(&mut space, "(: ax-mp (-> (→ $p $q) $p $q))");
        counted_insert_expr(&mut space, &fact).unwrap();

        let query = normalize_query_text("(: $f (-> $a1 $a2 $b))".as_bytes()).unwrap();
        let query_expr = parse_single_expr(&mut space, &query).unwrap();
        let rows = counted_query_rows_detailed_packet_rows(&space, &query_expr).unwrap();

        assert_eq!(rows.factor_count, 1);
        assert_eq!(rows.rows.len(), 1);

        let row = &rows.rows[0];
        let mut off = 0usize;
        assert_eq!(read_u32(row, &mut off), 1);
        let binding_count = read_u32(row, &mut off);
        assert_eq!(binding_count, 4);

        let mut decoded = BTreeMap::<u16, (u8, u8, Vec<u8>)>::new();
        for _ in 0..binding_count {
            let slot = read_u16(row, &mut off);
            let env = row[off];
            off += 1;
            let ground = row[off];
            off += 1;
            let expr_len = read_u32(row, &mut off) as usize;
            let expr_bytes = row[off..off + expr_len].to_vec();
            off += expr_len;
            decoded.insert(slot, (env, ground, expr_bytes));
        }

        assert_eq!(decoded.get(&0).unwrap().0, 0);
        assert_eq!(decoded.get(&1).unwrap().0, 0);
        assert_eq!(decoded.get(&2).unwrap().0, 0);
        assert_eq!(decoded.get(&3).unwrap().0, 0);

        assert_eq!(decoded.get(&2).unwrap().2, vec![BRIDGE_VALUE_TAG_VARREF, 0]);
        assert_eq!(decoded.get(&3).unwrap().2, vec![BRIDGE_VALUE_TAG_VARREF, 1]);
        assert_ne!(decoded.get(&2).unwrap().2, decoded.get(&3).unwrap().2);

        let a1_bytes = &decoded.get(&1).unwrap().2;
        assert!(
            a1_bytes
                .windows(2)
                .any(|w| w == [BRIDGE_VALUE_TAG_VARREF, 0].as_slice())
        );
        assert!(
            a1_bytes
                .windows(2)
                .any(|w| w == [BRIDGE_VALUE_TAG_VARREF, 1].as_slice())
        );
    }

    #[test]
    fn counted_query_only_packet_rows_materialize_non_ground_rule_result() {
        fn read_u32(packet: &[u8], off: &mut usize) -> u32 {
            let value = u32::from_be_bytes(packet[*off..*off + 4].try_into().unwrap());
            *off += 4;
            value
        }

        fn read_u16(packet: &[u8], off: &mut usize) -> u16 {
            let value = u16::from_be_bytes(packet[*off..*off + 2].try_into().unwrap());
            *off += 2;
            value
        }

        let mut space = Space::new();
        let fact = parse_expr(&mut space, "(: ax-mp (-> (→ $p $q) $p $q))");
        counted_insert_expr(&mut space, &fact).unwrap();

        let query = normalize_query_text("(: $f (-> $a1 $a2 $b))".as_bytes()).unwrap();
        let query_expr = parse_single_expr(&mut space, &query).unwrap();
        let rows = counted_query_only_packet_rows(&space, &query_expr).unwrap();

        assert_eq!(rows.len(), 1);

        let row = &rows[0];
        let mut off = 0usize;
        assert_eq!(read_u32(row, &mut off), 0);
        let binding_count = read_u32(row, &mut off);
        assert_eq!(binding_count, 4);

        let mut decoded = BTreeMap::<u16, (u8, u8, Vec<u8>)>::new();
        for _ in 0..binding_count {
            let slot = read_u16(row, &mut off);
            let env = row[off];
            off += 1;
            let ground = row[off];
            off += 1;
            let expr_len = read_u32(row, &mut off) as usize;
            let expr_bytes = row[off..off + expr_len].to_vec();
            off += expr_len;
            decoded.insert(slot, (env, ground, expr_bytes));
        }

        assert_eq!(decoded.get(&0).unwrap().0, 0);
        assert_eq!(decoded.get(&1).unwrap().0, 0);
        assert_eq!(decoded.get(&2).unwrap().0, 0);
        assert_eq!(decoded.get(&3).unwrap().0, 0);

        assert_eq!(decoded.get(&2).unwrap().2, vec![BRIDGE_VALUE_TAG_VARREF, 0]);
        assert_eq!(decoded.get(&3).unwrap().2, vec![BRIDGE_VALUE_TAG_VARREF, 1]);
        assert_ne!(decoded.get(&2).unwrap().2, decoded.get(&3).unwrap().2);
    }
}
