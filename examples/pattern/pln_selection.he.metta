!(import! &self lib_pln)
!(include pln_selection)
; Duplicate ca occurrences stay separate; matching does not revise confidence.
!(collapse (induce (select-induction &self A B)))
!(collapse (abduce (select-abduction &self A B)))
; Missing endpoints yield no selection. The evidence bag remains unchanged.
!(collapse (select-induction &self A absent))
!(collapse (match &self (evidence $id $claim $truth) $id))
