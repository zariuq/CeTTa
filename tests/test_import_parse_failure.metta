; Parse failures should surface as loader errors and rollback partial atoms.
(base)
!(collapse (import! &self support/import_parse_fail/module.metta))
!(collapse (match &self (base) (base)))
