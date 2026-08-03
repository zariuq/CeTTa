use mork::space::Space;
use mork_expr::{Expr, ExprEnv, apply_e, item_sink};
use pathmap::PathMap;
use std::collections::BTreeMap;

type ExprVar = (u8, u8);

fn instantiate_factor(factor: ExprEnv, bindings: &BTreeMap<ExprVar, ExprEnv>) -> Vec<u8> {
    let mut encoded = Vec::new();
    {
        let sink = item_sink(&mut encoded);
        let mut sink = std::pin::pin!(sink);
        apply_e(
            factor.n,
            factor.v,
            0,
            factor.subsexpr(),
            bindings,
            &mut sink,
            &mut BTreeMap::new(),
            &mut Vec::new(),
            &mut Vec::new(),
        );
    }
    encoded
}

fn query_bindings_from_references(primary: Expr, references: &[u32]) -> BTreeMap<ExprVar, ExprEnv> {
    references
        .iter()
        .enumerate()
        .map(|(variable, &offset)| {
            (
                (
                    0,
                    u8::try_from(variable)
                        .expect("MORK query variables are represented by eight-bit indices"),
                ),
                ExprEnv {
                    n: 254,
                    v: 0,
                    offset,
                    base: primary,
                },
            )
        })
        .collect()
}

pub fn query_multi_with_factor_exprs<F: FnMut(BTreeMap<(u8, u8), ExprEnv>, &[Expr]) -> bool>(
    btm: &PathMap<()>,
    pat_expr: Expr,
    mut effect: F,
) -> usize {
    let mut query_arguments = Vec::new();
    ExprEnv::new(0, pat_expr).args(&mut query_arguments);
    let factors = query_arguments.get(1..).unwrap_or_default().to_vec();

    Space::query_multi(btm, pat_expr, |references_or_bindings, primary| {
        let bindings = match references_or_bindings {
            Ok(references) => query_bindings_from_references(primary, references),
            Err(bindings) => bindings,
        };
        let encoded_factors = factors
            .iter()
            .map(|factor| instantiate_factor(*factor, &bindings))
            .collect::<Vec<_>>();
        let factor_exprs = encoded_factors
            .iter()
            .map(|encoded| Expr {
                ptr: encoded.as_ptr().cast_mut(),
            })
            .collect::<Vec<_>>();
        effect(bindings, &factor_exprs)
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(space: &mut Space, text: &[u8]) -> Vec<u8> {
        let mut encoded = vec![0; 256];
        let (_, used) = space
            .parse_sexpr(text, encoded.as_mut_ptr())
            .expect("test expression should parse");
        encoded.truncate(used);
        encoded
    }

    #[test]
    fn returns_each_join_factor_without_branch_only_mork_apis() {
        let mut space = Space::new();
        let first = parse(&mut space, b"(arc a b)");
        let second = parse(&mut space, b"(arc b c)");
        space.btm.insert(&first, ());
        space.btm.insert(&second, ());

        let query_bytes = parse(&mut space, b"(, (arc $u $v) (arc $v $w))");
        let query = Expr {
            ptr: query_bytes.as_ptr().cast_mut(),
        };
        let mut observed = Vec::new();
        let touched = query_multi_with_factor_exprs(&space.btm, query, |bindings, factor_exprs| {
            assert_eq!(bindings.len(), 3);
            assert_eq!(factor_exprs.len(), 2);
            observed.push(
                factor_exprs
                    .iter()
                    .map(|factor| unsafe {
                        factor
                            .span()
                            .as_ref()
                            .expect("factor expression should have a span")
                            .to_vec()
                    })
                    .collect::<Vec<_>>(),
            );
            true
        });

        assert_eq!(touched, 1);
        assert_eq!(observed, vec![vec![first, second]]);
    }

    #[test]
    fn does_not_call_effect_when_the_join_has_no_answer() {
        let mut space = Space::new();
        let fact = parse(&mut space, b"(arc a b)");
        space.btm.insert(&fact, ());

        let query_bytes = parse(&mut space, b"(, (arc a $v) (arc absent $w))");
        let query = Expr {
            ptr: query_bytes.as_ptr().cast_mut(),
        };
        let mut called = false;
        let touched = query_multi_with_factor_exprs(&space.btm, query, |_, _| {
            called = true;
            true
        });

        assert_eq!(touched, 0);
        assert!(!called);
    }
}
