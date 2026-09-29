; The forms of the compact TPTP morphism that belong to the strict official
; grammar: its disjunction is a list of literals, and introduced(T, I, P)
; always carries its parents.
(gslt-presentation-v1 TptpCompactStrictV1
  (signature
    (operator LCons 2)
    (operator bnf 2)
    (operator bnf 4)
    (operator metta-equation 2)
    (operator langdef:expression->list 1)
    (operator tptp-compact:disjuncts 2)
    (operator tptp-compact:elements 2)
    (operator tptp-compact:expression 1)
    (operator tptp-compact:info 2)
    (operator tptp-compact:parents 2)
    (operator tptp-compact:v 2))
  (equations)
  (rewrites
    (rule disjuncts-list
      (head (metta-equation (tptp-compact:disjuncts ?policy ?literals)
                            (langdef:expression->list (tptp-compact:v ?policy ?literals))))
      (body))
    (rule c-cnf_disjunction
      (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction ?list))
                            (tptp-compact:elements ?policy ?list)))
      (body))
    (rule c-internal_source
      (head (metta-equation (tptp-compact:v ?policy (bnf internal_source ?type ?info ?parents))
                            (tptp-compact:expression (LCons introduced (LCons (tptp-compact:v ?policy ?type) (LCons (tptp-compact:info ?policy ?info) (LCons (tptp-compact:parents ?policy ?parents) LNil)))))))
      (body))
  ))
