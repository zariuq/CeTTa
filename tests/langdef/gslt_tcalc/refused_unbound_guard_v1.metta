; A ground relation asked of an argument no premise before it binds.  Its
; tcalc guard would read a variable no pattern binds, which tcalc refuses;
; the tcalc target refuses the rule first, at the GSLT rule.
(gslt-presentation-v1 TcalcRefusedUnboundGuardV1
  (signature
    (operator different 2)
    (operator Apart 2)
    (operator oslf-external-relation-decl-v1 2))
  (equations)
  (rewrites
    (rule different-capability-v1
      (head (oslf-external-relation-decl-v1 different 2))
      (body))
    (rule apart-v1
      (head (Apart ?x Yes))
      (body (different ?x ?y)))))
