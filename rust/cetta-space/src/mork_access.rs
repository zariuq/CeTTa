use mork_expr::{Bindings, ExprEnv, UnificationFailure};
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
/// A callback may reenter the bridge on the same thread; the outer call keeps
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

#[cfg(test)]
mod tests {
    use super::with_mork_access;

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
