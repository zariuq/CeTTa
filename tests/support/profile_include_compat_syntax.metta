; he-compat mirrors Rust HE's one-argument include result syntax.

!(include include_simple)
!(assertEqualToResult (match &self (inc-simple $x) $x) (ok))
