!(import! &self mi_transaction_self)
!(module-rows)
!(tx-add after)
!(module-rows)
!(let $s (tx-self) (get-type $s))
!(case (tx-reject) (((Error $call $reason) rejected) ($other unexpected)))
!(module-rows)
; The importer has not acquired the module's rows as its own stored atoms.
!(collapse (get-atoms &self))
