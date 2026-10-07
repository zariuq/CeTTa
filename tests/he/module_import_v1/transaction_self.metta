!(import! &self mi_transaction_self)
!(module-rows)
!(tx-add after)
!(module-rows)
!(get-type (tx-self))
!(case (tx-reject) (((Error $call $reason) rejected) ($other unexpected)))
!(module-rows)
; The importer has not acquired the module's rows as its own stored atoms.
!(collapse (get-atoms &self))
