; Candidate census probe: a structured argument with a caller-bound field
; supplies no inner selector evidence to a deterministic caller.

(: keep-number (-[det]-> Number Number))
(= (keep-number $value) $value)

(: select-fixed-cell
   (-[$effect]-> (Cell Atom Number) (-[$effect]-> Number Number) Number))
(= (select-fixed-cell (Cell fixed $value) $continuation)
   ($continuation $value))

(: fixed-cell-caller (-[det]-> Atom Number))
(= (fixed-cell-caller $tag)
   (select-fixed-cell (Cell $tag 1) keep-number))

!(test (fixed-cell-caller fixed) 1)
!(test (collapse (fixed-cell-caller other)) ())
