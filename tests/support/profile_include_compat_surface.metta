; he-compat mirrors Rust HE's one-argument include result surface.

!(include include_simple)
!(assertEqualToResult (match &self (inc-simple $x) $x) (ok))
