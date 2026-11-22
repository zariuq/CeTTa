; A ground relation tcalc has no realization of.  The tcalc target reads a
; ground relation as tcalc's guards, fixed once for the dialect; a rule that
; asks one it has no reading of is refused, never compiled to no answer.
(gslt-presentation-v1 TcalcRefusedUnrealizedV1
  (signature
    (operator mystery 1)
    (operator Ask 2)
    (operator oslf-external-relation-decl-v1 2))
  (equations)
  (rewrites
    (rule mystery-capability-v1
      (head (oslf-external-relation-decl-v1 mystery 1))
      (body))
    (rule ask-v1
      (head (Ask ?x Yes))
      (body (mystery ?x)))))
