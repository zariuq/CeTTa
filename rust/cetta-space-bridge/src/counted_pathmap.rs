use super::{expr_span_bytes, parse_single_expr, validate_expr_bytes};
use mork::space::Space;
use mork_expr::{Expr, ExprEnv, Tag, byte_item, serialize, unify};
use pathmap::zipper::{Zipper, ZipperAbsolutePath, ZipperIteration, ZipperMoving};
use std::collections::BTreeMap;

#[cfg(test)]
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct CountedQueryRow {
    pub bindings: Vec<(u8, String)>,
    pub multiplicity: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct CountedEntry {
    pub atom_expr_bytes: Vec<u8>,
    pub count: u32,
    pub full_key: Vec<u8>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct CountedDetailedRow {
    pub bindings: Vec<(u8, String)>,
    pub factor_counts: Vec<u32>,
}

#[derive(Debug, Clone, Copy)]
struct DecodedCountedKey<'a> {
    atom_expr_bytes: &'a [u8],
    count: u32,
}

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

fn counted_key_for_atom_with_count(
    space: &mut Space,
    atom_expr_bytes: &[u8],
    count: u32,
) -> Result<Vec<u8>, String> {
    validate_expr_bytes(atom_expr_bytes)?;
    if count == 0 {
        return Err("counted PathMap keys require a positive count".to_string());
    }
    if count == 1 {
        return Ok(atom_expr_bytes.to_vec());
    }
    let count_expr_bytes = parse_single_expr(space, count.to_string().as_bytes())?;
    let mut key = Vec::with_capacity(atom_expr_bytes.len() + count_expr_bytes.len());
    key.extend_from_slice(atom_expr_bytes);
    key.extend_from_slice(&count_expr_bytes);
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

    let count_expr_bytes = &key[atom_expr_bytes.len()..];
    validate_expr_bytes(count_expr_bytes)?;
    let count_text = serialize(count_expr_bytes);
    let count = count_text.parse::<u32>().map_err(|_| {
        format!("counted PathMap key count expr must be a bare u32, got `{count_text}`")
    })?;
    if count == 0 {
        return Err("counted PathMap keys must not encode zero multiplicity".to_string());
    }

    Ok(DecodedCountedKey {
        atom_expr_bytes,
        count,
    })
}

pub(crate) fn counted_exact_entry(
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

pub(crate) fn counted_entries(space: &Space) -> Result<Vec<CountedEntry>, String> {
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

pub(crate) fn counted_insert_expr(space: &mut Space, atom_expr_bytes: &[u8]) -> Result<u32, String> {
    validate_expr_bytes(atom_expr_bytes)?;
    let current = counted_exact_entry(space, atom_expr_bytes)?;
    let next_count = current.as_ref().map(|entry| entry.count).unwrap_or(0).saturating_add(1);
    if let Some(entry) = current {
        space.btm.remove(&entry.full_key);
    }
    let next_key = counted_key_for_atom_with_count(space, atom_expr_bytes, next_count)?;
    space.btm.insert(&next_key, ());
    Ok(next_count)
}

pub(crate) fn counted_remove_one_expr(
    space: &mut Space,
    atom_expr_bytes: &[u8],
) -> Result<Option<u32>, String> {
    validate_expr_bytes(atom_expr_bytes)?;
    let Some(current) = counted_exact_entry(space, atom_expr_bytes)? else {
        return Ok(None);
    };
    space.btm.remove(&current.full_key);
    if current.count == 1 {
        return Ok(Some(0));
    }
    let next_count = current.count - 1;
    let next_key = counted_key_for_atom_with_count(space, atom_expr_bytes, next_count)?;
    space.btm.insert(&next_key, ());
    Ok(Some(next_count))
}

#[cfg(test)]
pub(crate) fn counted_unique_size(space: &Space) -> u64 {
    space.btm.val_count() as u64
}

pub(crate) fn counted_logical_size(space: &Space) -> Result<u64, String> {
    let mut rz = space.btm.read_zipper();
    let mut total = 0u64;
    while rz.to_next_val() {
        total = total.saturating_add(decode_counted_key(rz.path())?.count as u64);
    }
    Ok(total)
}

pub(crate) fn counted_factor_candidates(
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
        if unify(vec![(ExprEnv::new(0, factor_expr), ExprEnv::new(1, atom_expr))]).is_ok() {
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
        if unify(vec![(ExprEnv::new(0, factor_expr), ExprEnv::new(1, atom_expr))]).is_ok() {
            out.push(CountedEntry {
                atom_expr_bytes: decoded.atom_expr_bytes.to_vec(),
                count: decoded.count,
                full_key: full_key.to_vec(),
            });
        }
    }
    Ok(out)
}

fn query_binding_signature(bindings: &BTreeMap<(u8, u8), ExprEnv>) -> Vec<(u8, String)> {
    bindings
        .iter()
        .filter_map(|(&(side, idx), expr_env)| {
            if side != 0 {
                return None;
            }
            Some((idx, serialize(expr_span_bytes(expr_env.subsexpr()))))
        })
        .collect()
}

fn accumulate_counted_query_rows_detailed(
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
            rows.push(CountedDetailedRow {
                bindings: query_binding_signature(&bindings),
                factor_counts,
            });
        }
        return;
    }

    for idx in 0..candidate_lists[depth].len() {
        chosen.push(idx);
        accumulate_counted_query_rows_detailed(
            factors,
            candidate_lists,
            depth + 1,
            chosen,
            rows,
        );
        chosen.pop();
    }
}

pub(crate) fn counted_query_rows_detailed(
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
        factors,
        &candidate_lists,
        0,
        &mut chosen,
        &mut rows,
    );

    Ok(rows)
}

#[cfg(test)]
pub(crate) fn counted_query_rows(
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
        let slot = aggregated.entry(row.bindings).or_insert(0);
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
    use crate::normalize_query_text;

    fn parse_expr(space: &mut Space, text: &str) -> Vec<u8> {
        parse_single_expr(space, text.as_bytes()).expect("expression should parse")
    }

    #[test]
    fn counted_key_roundtrip_keeps_atom_prefix_and_count_expr() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(edge a b)");
        let key =
            counted_key_for_atom_with_count(&mut space, &atom, 3).expect("key should encode");
        let decoded = decode_counted_key(&key).expect("key should decode");
        let count_expr_bytes = &key[decoded.atom_expr_bytes.len()..];

        assert!(key.starts_with(atom.as_slice()));
        assert_eq!(decoded.atom_expr_bytes, atom.as_slice());
        assert_eq!(serialize(count_expr_bytes), "3");
        assert_eq!(decoded.count, 3);
    }

    #[test]
    fn counted_key_for_one_is_plain_atom_bytes() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(edge a b)");
        let key =
            counted_key_for_atom_with_count(&mut space, &atom, 1).expect("key should encode");
        let decoded = decode_counted_key(&key).expect("key should decode");
        let count_expr_bytes = &key[decoded.atom_expr_bytes.len()..];

        assert_eq!(key, atom);
        assert_eq!(decoded.atom_expr_bytes, atom.as_slice());
        assert!(count_expr_bytes.is_empty());
        assert_eq!(decoded.count, 1);
    }

    #[test]
    fn ordinary_pair_expr_is_not_atom_prefix_preserving() {
        let mut space = Space::new();
        let atom = parse_expr(&mut space, "(edge a b)");
        let pair = parse_expr(&mut space, "((edge a b) 3)");
        let atom_expr = Expr {
            ptr: atom.as_ptr().cast_mut(),
        };
        let atom_prefix = unsafe { atom_expr.prefix().unwrap_or_else(|full| full).as_ref().unwrap() };

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

        assert_eq!(counted_remove_one_expr(&mut space, &dup_a).unwrap(), Some(1));
        assert_eq!(counted_unique_size(&space), 2);
        assert_eq!(counted_logical_size(&space).unwrap(), 2);

        assert_eq!(counted_remove_one_expr(&mut space, &dup_a).unwrap(), Some(0));
        assert_eq!(counted_unique_size(&space), 1);
        assert_eq!(counted_logical_size(&space).unwrap(), 1);
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
}
