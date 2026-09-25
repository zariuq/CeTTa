; The forms of the compact TPTP morphism that belong to the corpus-compatible
; grammar.  Its disjunction admits a parenthesized equality or inequality
; beside another literal, so a disjunction is a chain of cnf_disjunction
; constructors whose disjuncts are its literals in order; a single literal is
; its own disjunct.  introduced(T, I) without parents is (introduced T I).
(gslt-presentation-v1 TptpCompactCorpusCompatibleV1
  (signature
    (operator LCons 2)
    (operator bnf 3)
    (operator bnf 4)
    (operator metta-equation 2)
    (operator tptp-compact:append 2)
    (operator tptp-compact:disjuncts 2)
    (operator tptp-compact:disjuncts-at 3)
    (operator tptp-compact:disjuncts-before 3)
    (operator tptp-compact:disjuncts-before-at 4)
    (operator tptp-compact:expression 1)
    (operator tptp-compact:info 2)
    (operator tptp-compact:kind 1)
    (operator tptp-compact:one-of 2)
    (operator tptp-compact:pair 3)
    (operator tptp-compact:parents 2)
    (operator tptp-compact:then 3)
    (operator tptp-compact:v 2))
  (equations)
  (rewrites
    (rule disjuncts
      (head (metta-equation (tptp-compact:disjuncts ?policy ?disjunction)
                            (tptp-compact:disjuncts-at ?policy
                              (tptp-compact:one-of (tptp-compact:kind ?disjunction)
                                (LCons cnf_disjunction/1 (LCons cnf_disjunction/2 (LCons cnf_disjunction/3
                                (LCons cnf_disjunction/4 (LCons cnf_disjunction/5 (LCons cnf_disjunction/6
                                (LCons cnf_disjunction/7 (LCons cnf_disjunction/8 (LCons cnf_disjunction/9 LNil))))))))))
                              ?disjunction)))
      (body))
    (rule disjuncts-chain (head (metta-equation (tptp-compact:disjuncts-at ?policy LangDef:SameValue ?chain) (tptp-compact:v ?policy ?chain))) (body))
    (rule disjuncts-literal (head (metta-equation (tptp-compact:disjuncts-at ?policy LangDef:DifferentValue ?literal) (LCons (tptp-compact:v ?policy ?literal) LNil))) (body))
    ; The disjuncts of a disjunction followed by one more literal.  A chain of
    ; disjunctions nests to the left; it is walked down its left spine once
    ; and the later literals gathered in order, so a clause of n literals takes
    ; n steps.
    (rule then
      (head (metta-equation (tptp-compact:then ?policy ?disjunction ?literal)
                            (tptp-compact:disjuncts-before ?policy ?disjunction (LCons (tptp-compact:v ?policy ?literal) LNil))))
      (body))
    (rule disjuncts-before
      (head (metta-equation (tptp-compact:disjuncts-before ?policy ?disjunction ?later)
                            (tptp-compact:disjuncts-before-at ?policy
                              (tptp-compact:one-of (tptp-compact:kind ?disjunction)
                                (LCons cnf_disjunction/1 (LCons cnf_disjunction/4 (LCons cnf_disjunction/5 LNil))))
                              ?disjunction ?later)))
      (body))
    (rule disjuncts-before-then
      (head (metta-equation (tptp-compact:disjuncts-before-at ?policy LangDef:SameValue (bnf ?name ?disjunction ?literal) ?later)
                            (tptp-compact:disjuncts-before ?policy ?disjunction (LCons (tptp-compact:v ?policy ?literal) ?later))))
      (body))
    (rule disjuncts-before-other
      (head (metta-equation (tptp-compact:disjuncts-before-at ?policy LangDef:DifferentValue ?disjunction ?later)
                            (tptp-compact:append (tptp-compact:disjuncts ?policy ?disjunction) ?later)))
      (body))
    (rule pair
      (head (metta-equation (tptp-compact:pair ?policy ?first ?second)
                            (LCons (tptp-compact:v ?policy ?first) (LCons (tptp-compact:v ?policy ?second) LNil))))
      (body))
    (rule c-cnf_disjunction/1 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/1 ?d ?l)) (tptp-compact:then ?policy ?d ?l))) (body))
    (rule c-cnf_disjunction/2 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/2 ?a ?b)) (tptp-compact:pair ?policy ?a ?b))) (body))
    (rule c-cnf_disjunction/3 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/3 ?a ?b)) (tptp-compact:pair ?policy ?a ?b))) (body))
    (rule c-cnf_disjunction/4 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/4 ?d ?l)) (tptp-compact:then ?policy ?d ?l))) (body))
    (rule c-cnf_disjunction/5 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/5 ?d ?l)) (tptp-compact:then ?policy ?d ?l))) (body))
    (rule c-cnf_disjunction/6 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/6 ?a ?b)) (tptp-compact:pair ?policy ?a ?b))) (body))
    (rule c-cnf_disjunction/7 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/7 ?a ?b)) (tptp-compact:pair ?policy ?a ?b))) (body))
    (rule c-cnf_disjunction/8 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/8 ?a ?b)) (tptp-compact:pair ?policy ?a ?b))) (body))
    (rule c-cnf_disjunction/9 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_disjunction/9 ?a ?b)) (tptp-compact:pair ?policy ?a ?b))) (body))
    (rule c-internal_source/0
      (head (metta-equation (tptp-compact:v ?policy (bnf internal_source/0 ?type ?info ?parents))
                            (tptp-compact:expression (LCons introduced (LCons (tptp-compact:v ?policy ?type) (LCons (tptp-compact:info ?policy ?info) (LCons (tptp-compact:parents ?policy ?parents) LNil)))))))
      (body))
    (rule c-internal_source/1
      (head (metta-equation (tptp-compact:v ?policy (bnf internal_source/1 ?type ?info))
                            (tptp-compact:expression (LCons introduced (LCons (tptp-compact:v ?policy ?type) (LCons (tptp-compact:info ?policy ?info) LNil))))))
      (body))
  ))
