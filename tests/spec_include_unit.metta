; Spec-backed test: HE stdlib docs say include returns Unit, even though the
; current HE Rust implementation returns the last inline eval or [].
!(include include_simple)
!(assertEqualToResult (match &self (inc-simple $x) $x) (ok))
