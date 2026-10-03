; A module whose loading fails leaves no trace: the importer links nothing,
; no part of the module stays loaded, and a later import loads it afresh.
; Upstream's runner cannot continue after a caught failed import, so this
; expectation is CeTTa's own.
!(case (import! &self mi_baddirective) (((Error $a $b) caught) ($_ fine)))
!(collapse (match &self (after-error $x) $x))
!(collapse (get-atoms &self))
!(case (import! &self mi_badparse) (((Error $a $b) caught) ($_ fine)))
!(import! &self mi_moda)
!(collapse (match &self (a-fact $x) $x))
!(case (import! &self mi_baddirective) (((Error $a $b) caught) ($_ fine)))
!(collapse (match &self (after-error $x) $x))
!(collapse (match (mod-space! mi_moda) (a-fact $x) $x))
