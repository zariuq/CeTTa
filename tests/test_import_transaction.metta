; CeTTa's module boundary should be transactional:
; - failed imports into &self must not leak partial atoms
; - failed imports into a fresh &name must not commit the registry binding
(base)
!(collapse (import! &self support/import_fail/module.metta))
!(collapse (match &self (base) (base)))
!(collapse (match &self (leaked-before-error) (leaked-before-error)))
!(collapse (import! &freshfail support/import_fail/module.metta))
!(collapse (get-atoms &freshfail))
