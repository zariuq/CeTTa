use mork::space::Space;
use mork_expr::{Expr, ExprEnv};
use pathmap::PathMap;
use std::collections::BTreeMap;

pub fn query_multi_with_factor_exprs<F: FnMut(BTreeMap<(u8, u8), ExprEnv>, &[Expr]) -> bool>(
    btm: &PathMap<()>,
    pat_expr: Expr,
    effect: F,
) -> usize {
    Space::query_multi_with_factor_exprs(btm, pat_expr, effect)
}
