; Cyclic imports should fail explicitly and rollback partial atoms.
(base)
!(collapse (import! &self support/import_cycle/A.metta))
!(collapse (match &self (base) (base)))
!(collapse (match &self (cycle-a-before) (cycle-a-before)))
!(collapse (match &self (cycle-b-before) (cycle-b-before)))
