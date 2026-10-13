use mork::space::Space;
use mork_expr::{Bindings, Expr, ExprEnv, UnificationFailure};
use pathmap::PathMap;
use std::cell::Cell;
use std::sync::Mutex;

static MORK_ACCESS: Mutex<()> = Mutex::new(());
thread_local! {
    static MORK_ACCESS_HELD: Cell<bool> = const { Cell::new(false) };
}

/// Serialize calls that touch MORK's process-global, non-atomic statistics.
///
/// This includes expression unification as well as kernel queries and execution.
/// Pure PathMap reads and mutations of independently owned spaces need no gate.
/// A callback may reenter this synchronization boundary on the same thread; the outer call keeps
/// the gate until all nested work has finished. Direct upstream calls outside
/// this boundary require the same synchronization discipline.
pub fn with_mork_access<T>(operation: impl FnOnce() -> T) -> T {
    if MORK_ACCESS_HELD.get() {
        return operation();
    }
    let _lock = MORK_ACCESS.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    struct Held;
    impl Drop for Held {
        fn drop(&mut self) {
            MORK_ACCESS_HELD.set(false);
        }
    }
    MORK_ACCESS_HELD.set(true);
    let _held = Held;
    operation()
}

pub fn bridge_unify(
    stack: &mut Vec<(ExprEnv, ExprEnv)>,
) -> Result<Bindings, UnificationFailure> {
    with_mork_access(|| mork_expr::unify(stack))
}

/// Query through the bridge's synchronization boundary.
///
/// A consumer that returns `false` receives no further answers, and the walk
/// is left to finish. Upstream's own callback-false exit is a `longjmp` taken
/// while the current answer's bindings are live. Normal completion instead
/// lets those bindings and traversal temporaries be dropped. Returns the total
/// number of answers found, which can exceed the number delivered to the consumer.
pub fn bridge_query_multi(
    space: &PathMap<()>,
    pattern: Expr,
    mut effect: impl FnMut(Result<&[u32], &Bindings>, Expr) -> bool,
) -> usize {
    let mut open = true;
    with_mork_access(|| {
        Space::query_multi(space, pattern, move |bindings, matched| {
            if open && !effect(bindings, matched) {
                open = false;
            }
            true
        })
    })
}

#[cfg(test)]
mod tests {
    use super::{bridge_query_multi, with_mork_access};
    use crate::bridge_parse_single_expr;
    use mork::space::Space;
    use mork_expr::Expr;

    #[test]
    fn queries_stop_delivery_and_preserve_later_queries_and_callback_panics() {
        let mut space = Space::new();
        for text in [b"(pair a a)".as_slice(), b"(pair a b)", b"(pair b b)"] {
            let fact = bridge_parse_single_expr(&mut space, text).unwrap();
            space.btm.insert(&fact, ());
        }
        let pattern = bridge_parse_single_expr(&mut space, b"(, (pair $x $x))").unwrap();
        let query = Expr { ptr: pattern.as_ptr().cast_mut() };
        for stop_at in [1, 2, 3] {
            let mut seen = 0;
            let touched = bridge_query_multi(&space.btm, query, |_, _| {
                seen += 1;
                with_mork_access(|| ());
                seen < stop_at
            });
            assert_eq!(seen, stop_at.min(2));
            // The walk finishes after the consumer stops, so it still finds both answers.
            assert_eq!(touched, 2);
            let mut after = 0;
            assert_eq!(bridge_query_multi(&space.btm, query, |_, _| {
                after += 1;
                true
            }), 2);
            assert_eq!(after, 2);
        }
        let empty = bridge_parse_single_expr(&mut space, b"(, (missing $x))").unwrap();
        assert_eq!(bridge_query_multi(&space.btm, Expr { ptr: empty.as_ptr().cast_mut() },
                                     |_, _| panic!("empty query must not call the effect")), 0);
        let unit = bridge_parse_single_expr(&mut space, b"(,)").unwrap();
        assert_eq!(bridge_query_multi(&space.btm, Expr { ptr: unit.as_ptr().cast_mut() },
                                     |_, _| false), 1);

        let failed = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            bridge_query_multi(&space.btm, query, |_, _| {
                std::panic::panic_any(17u32)
            })
        }));
        assert_eq!(*failed.unwrap_err().downcast::<u32>().unwrap(), 17);
        assert_eq!(bridge_query_multi(&space.btm, query, |_, _| true), 2);
    }

    #[test]
    fn callbacks_can_reenter_and_panics_do_not_block_later_workers() {
        assert_eq!(with_mork_access(|| with_mork_access(|| 42)), 42);
        let failed = std::panic::catch_unwind(|| {
            with_mork_access(|| with_mork_access(|| panic!("callback failure")))
        });
        assert!(failed.is_err());
        assert_eq!(
            std::thread::spawn(|| with_mork_access(|| 43)).join().unwrap(),
            43,
        );
    }
}
