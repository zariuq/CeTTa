; Comma is associative conjunction. A stored comma-shaped datum does not
; replace the separate facts required by its nested query.
!(bind! &s (new-space))
!(add-atom &s (p a))
!(add-atom &s (p b))
!(add-atom &s (, (r a) (q a)))
(= (entry-nested) (match &s (, (p $x) (, (r $x) (q $x))) $x))
!(collapse (let $answer (entry-nested) (let $written (add-atom &s (, (r b) (q b))) $answer)))
; The comma-shaped data still do not supply the separate facts.
!(collapse (match &s (, (p $x) (, (r $x) (q $x))) $x))
; Separate facts supply the conjunction, independent of grouping.
!(add-atom &s (p c))
!(add-atom &s (r c))
!(add-atom &s (q c))
!(collapse (match &s (, (p c) (, (r c) (q c))) found))
