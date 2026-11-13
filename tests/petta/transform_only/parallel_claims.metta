; Of several branches removing the same row, exactly one is told True.
!(bind! &token (new-space))
!(bind! &winners (new-space))
!(add-atom &token token)
(= (claim $i) (if (remove-atom &token token) (add-atom &winners $i) ()))
!(hyperpose ((claim 1) (claim 2) (claim 3) (claim 4) (claim 5) (claim 6) (claim 7) (claim 8)))
!(test (size-atom (collapse (match &winners $w $w))) 1)

; Rows added by parallel branches are all stored.
!(bind! &seen (new-space))
(= (note $i) (add-atom &seen (seen $i)))
!(hyperpose ((note 1) (note 2) (note 3) (note 4) (note 5) (note 6) (note 7) (note 8)))
!(test (size-atom (collapse (match &seen (seen $i) $i))) 8)
