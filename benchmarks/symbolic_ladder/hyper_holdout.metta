; Structured hyperedges. The label is data, not a join key.
; A node that is the target of two hedges is reported twice.
; A hedge into a missing node answers nothing.

(hedge a (lbl p) b)
(hedge a (lbl q) c)
(hedge d (lbl r) b)
(hedge e (lbl p) missing)
(node b)
(node c)

!(match &self (, (hedge $x $lab $y) (node $y)) (h $x $lab $y))
!(match &self (hedge e (lbl p) missing) (absent e))
