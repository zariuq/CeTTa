!(import! &self (library lib_pln))
!(include pln_selection)
; PeTTa retains its own library-import idiom and confidence formula.
!(collapse (induce (select-induction &self A B)))
!(collapse (abduce (select-abduction &self A B)))
!(collapse (select-induction &self A absent))
!(collapse (match &self (evidence $id $claim $truth) $id))
