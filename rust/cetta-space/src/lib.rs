use mork::space::Space;
use mork_expr::{Expr, ExprEnv, ExprZipper, Tag, item_byte, maybe_byte_item};
use mork_frontend::bytestring_parser::{Context, Parser, ParserError};
use mork_interning::WritePermit;
use std::cell::RefCell;
use std::collections::HashMap;
use std::sync::{Mutex, OnceLock};

mod counted_pathmap;
#[cfg(feature = "pathmap-space")]
mod counted_query_index;

pub use counted_pathmap::{
    CountedDetailedPacketRows, CountedDetailedRow, CountedEntry, CountedGeneralQueryCursor,
    counted_contains_expr, counted_entries, counted_exact_entry, counted_expr_row_packet,
    counted_factor_candidates, counted_insert_expr, counted_insert_expr_batch,
    counted_insert_expr_batch_cached, counted_insert_expr_cached, counted_insert_expr_count_cached,
    counted_logical_size, counted_query_only_packet_rows, counted_query_rows_detailed,
    counted_query_rows_detailed_packet_rows, counted_remove_expr_batch,
    counted_remove_expr_batch_cached, counted_remove_one_expr, counted_remove_one_expr_cached,
    counted_sexpr_text, counted_sync_cached_logical_size, counted_unique_size,
};

#[cfg(test)]
pub use counted_pathmap::CountedQueryRow;

#[cfg(feature = "pathmap-space")]
pub use counted_query_index::{
    FlatCountedCursorStats, FlatCountedIndexStats, FlatCountedQueryAdmission,
    FlatCountedQueryCursor, FlatCountedQueryIndex, FlatSemiNaiveQueryCursor,
};

struct BridgeExprParser<'a> {
    small_buf: [u8; 64],
    large_buf: [u8; 8],
    write_permit: WritePermit<'a>,
}

impl Parser for BridgeExprParser<'_> {
    fn tokenizer<'r>(&mut self, s: &[u8]) -> &'r [u8] {
        if s.len() > 63 {
            self.large_buf = self.write_permit.get_sym_or_insert(s);
            return unsafe { std::mem::transmute(&self.large_buf[..]) };
        }
        self.small_buf[..s.len()].copy_from_slice(s);
        unsafe { std::mem::transmute(&self.small_buf[..s.len()]) }
    }
}

impl<'a> BridgeExprParser<'a> {
    fn new(space: &'a Space) -> Result<Self, String> {
        let write_permit = space
            .sm
            .try_aquire_permission()
            .map_err(|_| "failed to acquire bridge symbol-table write permission".to_string())?;
        Ok(Self {
            small_buf: [0; 64],
            large_buf: [0; 8],
            write_permit,
        })
    }
}

/// Parse exactly one expression, in a fresh binder scope.
///
/// MM2/MORK variables are EXPRESSION-LOCAL: `Tag::NewVar` marks a variable's first
/// occurrence within an expression, and `Tag::VarRef(i)` indexes the `NewVar`s introduced
/// within that same expression. The scope must therefore be reset for every expression --
/// MORK does this in its loader (`MORK/kernel/src/space.rs`, `it.variables.clear()` at the
/// end of each `load_all_sexpr_impl` iteration). Carrying one table across a whole file
/// shifts every later expression's indices and yields dangling `VarRef`s.
///
/// This is the single place CeTTa establishes that scope; every parse entry point goes
/// through it, so the invariant cannot be lost by adding another caller. Clearing on entry
/// rather than exit also covers the first expression and survives any future early return.
fn parse_one_expr(
    parser: &mut BridgeExprParser<'_>,
    context: &mut Context<'_>,
    capacity: usize,
    what: &str,
) -> Result<Vec<u8>, String> {
    context.variables.clear();

    let mut parse_buffer = vec![0u8; capacity];
    let mut ez = ExprZipper::new(Expr {
        ptr: parse_buffer.as_mut_ptr(),
    });
    parser
        .sexpr(context, &mut ez)
        .map_err(|e| format!("{} parse failed: {:?}", what, e))?;
    parse_buffer.truncate(ez.loc);
    Ok(parse_buffer)
}

pub fn bridge_parse_single_expr(space: &mut Space, input: &[u8]) -> Result<Vec<u8>, String> {
    let capacity = input.len().saturating_mul(4).saturating_add(4096);
    let mut parser = BridgeExprParser::new(space)?;
    let mut context = Context::new(input);

    let parse_buffer = parse_one_expr(&mut parser, &mut context, capacity, "pattern")?;

    skip_trailing(&mut context).map_err(|e| format!("pattern trailing parse failed: {:?}", e))?;
    if context.loc < context.src.len() {
        return Err("pattern contains trailing non-whitespace input".to_string());
    }

    Ok(parse_buffer)
}

pub fn bridge_parse_expr_chunk(space: &mut Space, input: &[u8]) -> Result<Vec<Vec<u8>>, String> {
    let mut parser = BridgeExprParser::new(space)?;
    let mut context = Context::new(input);
    let mut out = Vec::new();

    loop {
        skip_trailing(&mut context).map_err(|e| format!("chunk trailing parse failed: {:?}", e))?;
        if context.loc >= context.src.len() {
            break;
        }

        let remaining = &context.src[context.loc..];
        let capacity = remaining.len().saturating_mul(4).saturating_add(4096);
        let parse_buffer = parse_one_expr(&mut parser, &mut context, capacity, "chunk")?;
        out.push(parse_buffer);
    }

    Ok(out)
}

#[cfg(test)]
fn parse_single_expr(space: &mut Space, input: &[u8]) -> Result<Vec<u8>, String> {
    bridge_parse_single_expr(space, input)
}

#[cfg(test)]
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

static BRIDGE_REF_NAMES: OnceLock<Mutex<HashMap<u8, &'static str>>> = OnceLock::new();
static BRIDGE_ENV_VAR_NAMES: OnceLock<Mutex<HashMap<(u8, u8), &'static str>>> = OnceLock::new();

fn bridge_ref_name(index: u8) -> &'static str {
    let cache = BRIDGE_REF_NAMES.get_or_init(|| Mutex::new(HashMap::new()));
    let mut guard = cache.lock().unwrap();
    if let Some(existing) = guard.get(&index) {
        return existing;
    }
    let leaked: &'static str = Box::leak(format!("_{}", index + 1).into_boxed_str());
    guard.insert(index, leaked);
    leaked
}

fn bridge_env_var_name(side: u8, index: u8) -> &'static str {
    let cache = BRIDGE_ENV_VAR_NAMES.get_or_init(|| Mutex::new(HashMap::new()));
    let mut guard = cache.lock().unwrap();
    if let Some(existing) = guard.get(&(side, index)) {
        return existing;
    }
    let leaked: &'static str = Box::leak(format!("$__mork_b{}_{}", side, index).into_boxed_str());
    guard.insert((side, index), leaked);
    leaked
}

const BRIDGE_VALUE_TAG_ARITY: u8 = 0x00;
const BRIDGE_VALUE_TAG_SYMBOL: u8 = 0x01;
const BRIDGE_VALUE_TAG_NEWVAR: u8 = 0x02;
const BRIDGE_VALUE_TAG_VARREF: u8 = 0x03;

fn append_u32_be(out: &mut Vec<u8>, value: u32) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn read_bridge_packet_u32(input: &[u8], offset: &mut usize) -> Result<u32, String> {
    if input.len().saturating_sub(*offset) < 4 {
        return Err("bridge expr packet truncated while reading u32".to_string());
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

/// Normalize the length-delimited bridge expression wire format into the
/// compact expression representation owned by `space`.
///
/// Symbols longer than the compact format's 63-byte inline limit are interned
/// in the target space and represented by their eight-byte symbol handle.
/// The input remains independent of any particular MORK symbol table.
pub fn bridge_expr_packet_to_bytes(space: &Space, input: &[u8]) -> Result<Vec<u8>, String> {
    if input.is_empty() {
        return Err("bridge expr packet must not be empty".to_string());
    }
    let write_permit = space
        .sm
        .try_aquire_permission()
        .map_err(|_| "failed to acquire bridge symbol-table write permission".to_string())?;
    let mut offset = 0usize;
    let mut pending = 1usize;
    let mut introduced_vars = 0usize;
    let mut compact = Vec::with_capacity(input.len());

    while pending > 0 {
        let Some(tag) = input.get(offset).copied() else {
            return Err("bridge expr packet truncated while reading tag".to_string());
        };
        offset += 1;
        pending -= 1;

        match tag {
            BRIDGE_VALUE_TAG_ARITY => {
                let arity = read_bridge_packet_u32(input, &mut offset)?;
                if arity >= 64 {
                    return Err(format!(
                        "bridge expr packet arity must be at most 63, got {arity}"
                    ));
                }
                compact.push(item_byte(Tag::Arity(arity as u8)));
                pending = pending
                    .checked_add(arity as usize)
                    .ok_or_else(|| "bridge expr packet arity overflow".to_string())?;
            }
            BRIDGE_VALUE_TAG_SYMBOL => {
                let symbol_len =
                    usize::try_from(read_bridge_packet_u32(input, &mut offset)?)
                        .map_err(|_| "bridge expr packet symbol length overflow".to_string())?;
                if symbol_len == 0 {
                    return Err("bridge expr packet symbols must not be empty".to_string());
                }
                let symbol_end = offset
                    .checked_add(symbol_len)
                    .ok_or_else(|| "bridge expr packet symbol length overflow".to_string())?;
                let symbol = input
                    .get(offset..symbol_end)
                    .ok_or_else(|| {
                        "bridge expr packet truncated while reading symbol bytes".to_string()
                    })?;
                offset = symbol_end;
                if symbol_len < 64 {
                    compact.push(item_byte(Tag::SymbolSize(symbol_len as u8)));
                    compact.extend_from_slice(symbol);
                } else {
                    let handle = write_permit.get_sym_or_insert(symbol);
                    compact.push(item_byte(Tag::SymbolSize(handle.len() as u8)));
                    compact.extend_from_slice(&handle);
                }
            }
            BRIDGE_VALUE_TAG_NEWVAR => {
                if introduced_vars >= 64 {
                    return Err(
                        "bridge expr packet supports at most 64 distinct variables".to_string()
                    );
                }
                compact.push(item_byte(Tag::NewVar));
                introduced_vars += 1;
            }
            BRIDGE_VALUE_TAG_VARREF => {
                let Some(index) = input.get(offset).copied() else {
                    return Err(
                        "bridge expr packet truncated while reading variable reference".to_string()
                    );
                };
                offset += 1;
                if index as usize >= introduced_vars {
                    return Err(format!(
                        "bridge expr packet references variable slot {index} before introduction"
                    ));
                }
                compact.push(item_byte(Tag::VarRef(index)));
            }
            other => {
                return Err(format!(
                    "bridge expr packet contains invalid tag 0x{other:02x}"
                ));
            }
        }
    }

    if offset != input.len() {
        return Err("bridge expr packet contains trailing data".to_string());
    }
    validate_expr_bytes(&compact)?;
    Ok(compact)
}

pub fn stable_bridge_expr_bytes(space: &Space, expr: Expr) -> Result<Vec<u8>, String> {
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
                        "stable bridge expr symbol must be 1..63 bytes, got {}",
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

    validate_expr_bytes(&encoded)?;
    Ok(encoded)
}

pub fn stable_bridge_expr_packet_bytes(space: &Space, expr: Expr) -> Result<Vec<u8>, String> {
    let sym_table = space.sym_table();
    let mut encoded = Vec::new();
    let mut ez = ExprZipper::new(expr);

    loop {
        match ez.item() {
            Ok(Tag::NewVar) => encoded.push(BRIDGE_VALUE_TAG_NEWVAR),
            Ok(Tag::VarRef(index)) => {
                encoded.push(BRIDGE_VALUE_TAG_VARREF);
                encoded.push(index);
            }
            Ok(Tag::Arity(arity)) => {
                encoded.push(BRIDGE_VALUE_TAG_ARITY);
                append_u32_be(&mut encoded, arity as u32);
            }
            Ok(Tag::SymbolSize(_)) => unreachable!("ExprZipper::item returns Err for symbol bytes"),
            Err(symbol) => {
                let bridge_symbol = if symbol.len() == 8 {
                    let mut handle = [0u8; 8];
                    handle.copy_from_slice(symbol);
                    sym_table.get_bytes(handle).unwrap_or(symbol)
                } else {
                    symbol
                };
                if bridge_symbol.is_empty() {
                    return Err("stable bridge expr packet symbol must not be empty".to_string());
                }
                encoded.push(BRIDGE_VALUE_TAG_SYMBOL);
                append_u32_be(&mut encoded, bridge_symbol.len() as u32);
                encoded.extend_from_slice(bridge_symbol);
            }
        }
        if !ez.next() {
            break;
        }
    }

    Ok(encoded)
}

pub fn bridge_expr_text(space: &Space, expr: Expr) -> Result<Vec<u8>, String> {
    let sym_table = space.sym_table();
    let error = RefCell::new(None::<String>);
    let mut out = Vec::new();
    expr.serialize2(
        &mut out,
        |s| {
            let resolved = if s.len() == 8 {
                let mut handle = [0u8; 8];
                handle.copy_from_slice(s);
                sym_table.get_bytes(handle).unwrap_or(s)
            } else {
                s
            };
            match std::str::from_utf8(resolved) {
                Ok(text) => unsafe { std::mem::transmute(text) },
                Err(err) => {
                    *error.borrow_mut() =
                        Some(format!("bridge expr symbol was not valid utf8: {err}"));
                    ""
                }
            }
        },
        |index, is_new| {
            if is_new { "$" } else { bridge_ref_name(index) }
        },
    );
    if let Some(err) = error.into_inner() {
        return Err(err);
    }
    Ok(out)
}

pub fn bridge_expr_env_text(space: &Space, expr_env: ExprEnv) -> Result<Vec<u8>, String> {
    let sym_table = space.sym_table();
    let error = RefCell::new(None::<String>);
    let mut out = Vec::new();
    expr_env.subsexpr().serialize2(
        &mut out,
        |s| {
            let resolved = if s.len() == 8 {
                let mut handle = [0u8; 8];
                handle.copy_from_slice(s);
                sym_table.get_bytes(handle).unwrap_or(s)
            } else {
                s
            };
            match std::str::from_utf8(resolved) {
                Ok(text) => unsafe { std::mem::transmute(text) },
                Err(err) => {
                    *error.borrow_mut() =
                        Some(format!("bridge env symbol was not valid utf8: {err}"));
                    ""
                }
            }
        },
        |index, _is_new| bridge_env_var_name(expr_env.n, index),
    );
    if let Some(err) = error.into_inner() {
        return Err(err);
    }
    Ok(out)
}

#[cfg(test)]
pub fn render_bridge_expr_text(expr_bytes: &[u8], value_env: u8) -> Result<String, String> {
    validate_expr_bytes(expr_bytes)?;
    let expr = Expr {
        ptr: expr_bytes.as_ptr().cast_mut(),
    };
    let mut out = Vec::new();
    expr.serialize2(
        &mut out,
        |s| unsafe { std::mem::transmute(std::str::from_utf8_unchecked(s)) },
        |index, _is_new| {
            Box::leak(format!("$__mork_b{}_{}", value_env, index + 1).into_boxed_str())
        },
    );
    String::from_utf8(out).map_err(|err| format!("bridge expr text was not utf8: {err}"))
}

#[cfg(test)]
mod bridge_packet_tests {
    use super::*;

    fn push_symbol(packet: &mut Vec<u8>, symbol: &[u8]) {
        packet.push(BRIDGE_VALUE_TAG_SYMBOL);
        packet.extend_from_slice(&(symbol.len() as u32).to_be_bytes());
        packet.extend_from_slice(symbol);
    }

    #[test]
    fn length_delimited_packet_interns_and_roundtrips_long_symbols() {
        let space = Space::new();
        let long = vec![b'x'; 256];
        let mut packet = vec![BRIDGE_VALUE_TAG_ARITY];
        packet.extend_from_slice(&2u32.to_be_bytes());
        push_symbol(&mut packet, b"doc");
        push_symbol(&mut packet, &long);

        let compact = bridge_expr_packet_to_bytes(&space, &packet).unwrap();
        let expr = Expr {
            ptr: compact.as_ptr().cast_mut(),
        };
        assert_eq!(stable_bridge_expr_packet_bytes(&space, expr).unwrap(), packet);
    }

    #[test]
    fn length_delimited_packet_rejects_unintroduced_variable_references() {
        let space = Space::new();
        let packet = [BRIDGE_VALUE_TAG_VARREF, 0];
        let err = bridge_expr_packet_to_bytes(&space, &packet).unwrap_err();
        assert!(err.contains("before introduction"));
    }
}
