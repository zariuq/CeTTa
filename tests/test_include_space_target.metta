!(bind! &kb (new-space))
!(include &kb include_simple)
!(assertEqualToResult (match &kb (inc-simple $x) $x) (ok))
!(assertEqualToResult (match &self (inc-simple $x) $x) ())

!(add-atom &kb (seed))
!(collapse (include &kb support/import_fail/module.metta))
!(collapse (match &kb (seed) (seed)))
!(collapse (match &kb (leaked-before-error) (leaked-before-error)))

!(include &self include_simple)
!(assertEqualToResult (match &self (inc-simple $x) $x) (ok))
