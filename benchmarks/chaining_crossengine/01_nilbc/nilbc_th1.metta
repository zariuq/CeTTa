;; Row 01 — Nil nilbc: Metamath demo0 th1 (t=t) by nested modus ponens.
;; Source: hyperon/PeTTa/examples/nilbc.metta (Nil Geisweiller). Faithful bc + Hard KB.
;; Depth knob: DEPTH placeholder in the query below (intended proof needs depth 5).
;; Runs on both CeTTa (--lang he) and PeTTa (run.sh).


;; Define Nat
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

;; Define cast functions between Nat and Number
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))
(: fromNat (-> Nat Number))
(= (fromNat Z) 0)
(= (fromNat (S $k)) (+ 1 (fromNat $k)))

;;;;;;;;;;;;;;;;;;;;;;
;; Backward chainer ;;
;;;;;;;;;;;;;;;;;;;;;;

;; Backward Chainer supporting dependent types.
;;
;; The arguments of the backward chainer are:
;;
;; * Knowledge base: pointer to a space containing axioms and rules in
;;   the format (: <NAME> <RULE>).  Rules may not be curried, meaning
;;   that a rule with n premises may be represented by
;;
;;   (: <NAME> (-> <PREMISE1> ... <PREMISEn> <CONCLUSION>))
;;
;;   In addition, each premise must be an inline typing relationship,
;;   to represent dependent types.  It should be noted that such
;;   typing relationship must be provided even if the witness is not
;;   present in the rest of the definition.  For instance <PREMISE1>
;;   would have the format
;;
;;   (: <TERM1> <TYPE1>)
;;
;; * Maximum depth: maximum depth of the generated proof tree.
;;
;; * Query: a metta term of the form (: <PROOF> <THEOREM>) where
;;   <PROOF> and <THEOREM> may contain free variables that may be
;;   filled by the backward chainer.
;;
;; A result is the query with its variables grounded, fully or
;; partially.  If multiple results are possible, they are returned as
;; a superposition.

(: bc (-> $a                            ; Knowledge base space
          Nat                           ; Maximum depth
          $b                            ; Query
          $b))                          ; Result

;; Base cases

;; Match the knowledge base
(= (bc $knowledge_base $_ (: $proof $theorem)) (match $knowledge_base (: $proof $theorem) (: $proof $theorem)))

;; Recursive steps

;; Unary proof application
(= (bc $knowledge_base (S $depth) (: ($proof_rule $premise_proof) $theorem))
   (let* (;; Recurse on abtraction
          ((: $proof_rule (-> (: $premise_proof $premise_type) $theorem))
           (bc $knowledge_base $depth (: $proof_rule (-> (: $premise_proof $premise_type) $theorem))))
          ;; Recurse on premise
          ((: $premise_proof $premise_type)
           (bc $knowledge_base $depth (: $premise_proof $premise_type))))
     ;; Output fulfilled query
     (: ($proof_rule $premise_proof) $theorem)))

;; Binary proof application
(= (bc $knowledge_base (S $depth) (: ($proof_rule $premise_proof1 $premise_proof2) $theorem))
   (let* (;; Recurse on abstraction
          ((: $proof_rule (-> (: $premise_proof1 $premise_type1)
                              (: $premise_proof2 $premise_type2)
                              $theorem))
           (bc $knowledge_base $depth (: $proof_rule (-> (: $premise_proof1 $premise_type1)
                                                          (: $premise_proof2 $premise_type2)
                                                          $theorem))))
          ;; Recurse on first premise
          ((: $premise_proof1 $premise_type1)
           (bc $knowledge_base $depth (: $premise_proof1 $premise_type1)))
          ;; Recurse on second premise
          ((: $premise_proof2 $premise_type2)
           (bc $knowledge_base $depth (: $premise_proof2 $premise_type2))))
     ;; Output fulfilled query
     (: ($proof_rule $premise_proof1 $premise_proof2) $theorem)))

;; Ternary proof application
(= (bc $knowledge_base (S $depth) (: ($proof_rule $premise_proof1 $premise_proof2 $premise_proof3) $theorem))
   (let* (;; Recurse on abstraction
          ((: $proof_rule (-> (: $premise_proof1 $premise_type1)
                              (: $premise_proof2 $premise_type2)
                              (: $premise_proof3 $premise_type3)
                              $theorem))
           (bc $knowledge_base $depth (: $proof_rule (-> (: $premise_proof1 $premise_type1)
                                                          (: $premise_proof2 $premise_type2)
                                                          (: $premise_proof3 $premise_type3)
                                                          $theorem))))
          ;; Recurse on first premise
          ((: $premise_proof1 $premise_type1)
           (bc $knowledge_base $depth (: $premise_proof1 $premise_type1)))
          ;; Recurse on second premise
          ((: $premise_proof2 $premise_type2)
           (bc $knowledge_base $depth (: $premise_proof2 $premise_type2)))
          ;; Recurse on third premise
          ((: $premise_proof3 $premise_type3)
           (bc $knowledge_base $depth (: $premise_proof3 $premise_type3))))
     ;; Output fulfilled query
     (: ($proof_rule $premise_proof1 $premise_proof2 $premise_proof3) $theorem)))

;; Quaternary proof application
(= (bc $knowledge_base (S $depth) (: ($proof_rule $premise_proof1 $premise_proof2 $premise_proof3 $premise_proof4) $theorem))
   (let* (;; Recurse on abstraction
          ((: $proof_rule (-> (: $premise_proof1 $premise_type1)
                              (: $premise_proof2 $premise_type2)
                              (: $premise_proof3 $premise_type3)
                              (: $premise_proof4 $premise_type4)
                              $theorem))
           (bc $knowledge_base $depth (: $proof_rule (-> (: $premise_proof1 $premise_type1)
                                                          (: $premise_proof2 $premise_type2)
                                                          (: $premise_proof3 $premise_type3)
                                                          (: $premise_proof4 $premise_type4)
                                                          $theorem))))
          ;; Recurse on first premise
          ((: $premise_proof1 $premise_type1)
           (bc $knowledge_base $depth (: $premise_proof1 $premise_type1)))
          ;; Recurse on second premise
          ((: $premise_proof2 $premise_type2)
           (bc $knowledge_base $depth (: $premise_proof2 $premise_type2)))
          ;; Recurse on third premise
          ((: $premise_proof3 $premise_type3)
           (bc $knowledge_base $depth (: $premise_proof3 $premise_type3)))
          ;; Recurse on fourth premise
          ((: $premise_proof4 $premise_type4)
           (bc $knowledge_base $depth (: $premise_proof4 $premise_type4))))
     ;; Output fulfilled query
     (: ($proof_rule $premise_proof1 $premise_proof2 $premise_proof3 $premise_proof4) $theorem)))

;; Quintenary proof application
(= (bc $knowledge_base (S $depth) (: ($proof_rule $premise_proof1 $premise_proof2 $premise_proof3 $premise_proof4 $premise_proof5) $theorem))
   (let* (;; Recurse on abstraction
          ((: $proof_rule (-> (: $premise_proof1 $premise_type1)
                              (: $premise_proof2 $premise_type2)
                              (: $premise_proof3 $premise_type3)
                              (: $premise_proof4 $premise_type4)
                              (: $premise_proof5 $premise_type5)
                              $theorem))
           (bc $knowledge_base $depth (: $proof_rule (-> (: $premise_proof1 $premise_type1)
                                                          (: $premise_proof2 $premise_type2)
                                                          (: $premise_proof3 $premise_type3)
                                                          (: $premise_proof4 $premise_type4)
                                                          (: $premise_proof5 $premise_type5)
                                                          $theorem))))
          ;; Recurse on first premise
          ((: $premise_proof1 $premise_type1)
           (bc $knowledge_base $depth (: $premise_proof1 $premise_type1)))
          ;; Recurse on second premise
          ((: $premise_proof2 $premise_type2)
           (bc $knowledge_base $depth (: $premise_proof2 $premise_type2)))
          ;; Recurse on third premise
          ((: $premise_proof3 $premise_type3)
           (bc $knowledge_base $depth (: $premise_proof3 $premise_type3)))
          ;; Recurse on fourth premise
          ((: $premise_proof4 $premise_type4)
           (bc $knowledge_base $depth (: $premise_proof4 $premise_type4)))
          ;; Recurse on fifth premise
          ((: $premise_proof5 $premise_type5)
           (bc $knowledge_base $depth (: $premise_proof5 $premise_type5))))
     ;; Output fulfilled query
     (: ($proof_rule $premise_proof1 $premise_proof2 $premise_proof3 $premise_proof4 $premise_proof5) $theorem)))

;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ;;;;;;;;;;;;;;;;;;;; ;;
;; ;; Knowledge base ;; ;;
;; ;;;;;;;;;;;;;;;;;;;; ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Convert
;; [demo0.mm](https://github.com/zariuq/mmverify.py/blob/metta/examples/demo0.mm)
;; to MeTTa using a type theoretic representation.
;;
;; There are four levels of difficulty, each one getting increasingly
;; closer to the Metamath representation.
;;
;; - Easy: term and wff are ignored.  Metamath implication is ignored
;;   and replaced by the arrow type.  Disjoint variables are not
;;   supported.
;;
;; - Medium: like easy but term and wff types are used.
;;
;; - Hard: like medium but metamath implication is used.
;;
;; - Nightmare: everything, term, wff, metamath implication and
;;   disjoint variables, is supported.

;;;;;;;;;;
;; Easy ;;
;;;;;;;;;;

!(bind! &kbe (new-space))

;; Informal description:
;;   Equality is right Euclidean (see https://arxiv.org/pdf/1806.05036)
;;
;; Metamath description:
;;   $( State Axiom ~ a1 . $)
;;   a1 $a |- ( t = r -> ( t = s -> r = s ) ) $.
;;
;; Warning: not using ⟨->⟩ and the fact that $t $r and $s are wff.
!(add-atom &kbe (: a1 (-> (: $ter (⟨=⟩ $t $r))
                          (: $tes (⟨=⟩ $t $s))
                          (⟨=⟩ $r $s))))

;; Informal description:
;;   Zero is a right identity of addition.
;;
;; Metamath description:
;;   $( State Axiom ~ a2 . $)
;;   a2 $a |- ( t + 0 ) = t $.
;;

!(bind! &kbh (new-space))

;; Informal description:
;;   Zero is a term
;;
;; Metamath description:
;;   $( Define "term" (part 1 of 2). $)
;;   tze $a term 0 $.
!(add-atom &kbh (: ⟨0⟩ ⟨term⟩))

;; Informal description:
;;   Adding two terms outputs a term
;;
;; Metamath description:
;;   $( Define "term" (part 2 of 2). $)
;;   tpl $a term ( t + r ) $.
!(add-atom &kbh (: ⟨+⟩ (-> (: $t ⟨term⟩)
                           (: $r ⟨term⟩)
                           ⟨term⟩)))

;; Informal description:
;;   Equality between two terms is a well formed formula.
;;
;; Metamath description:
;;   $( Define "wff" (part 1 of 2). $)
;;   weq $a wff t = r $.
!(add-atom &kbh (: ⟨=⟩ (-> (: $t ⟨term⟩)
                           (: $r ⟨term⟩)
                           ⟨wff⟩)))

;; Informal description:
;;   Implication between two well formed formulas is a well formed formula.
;;
;; Metamath description:
;;   $( Define "wff" (part 2 of 2). $)
;;   wim $a wff ( P -> Q ) $.
!(add-atom &kbh (: ⟨->⟩ (-> (: $P ⟨wff⟩)
                            (: $Q ⟨wff⟩)
                            ⟨wff⟩)))

;; Informal description:
;;   Equality is right Euclidean (see https://arxiv.org/pdf/1806.05036)
;;
;; Metamath description:
;;   $( State Axiom ~ a1 . $)
;;   a1 $a |- ( t = r -> ( t = s -> r = s ) ) $.
!(add-atom &kbh (: a1 (-> (: $t ⟨term⟩)
                          (: $r ⟨term⟩)
                          (: $s ⟨term⟩)
                          (⟨->⟩ (⟨=⟩ $t $r) (⟨->⟩ (⟨=⟩ $t $s) (⟨=⟩ $r $s))))))

;; Informal description:
;;   Zero is a right identity of addition.
;;
;; Metamath description:
;;   $( State Axiom ~ a2 . $)
;;   a2 $a |- ( t + 0 ) = t $.
!(add-atom &kbh (: a2 (-> (: $t ⟨term⟩)
                          (⟨=⟩ (⟨+⟩ $t ⟨0⟩) $t))))

;; Informal description:
;;   Modus ponens
;;
;; Metamath description:
;;   ${
;;     min $e |- P $.
;;     maj $e |- ( P -> Q ) $.
;;     $( Define the modus ponens inference rule. $)
;;     mp $a |- Q $.
;;   $}
;;
;; The major premise appears first to speed up proof search.
!(add-atom &kbh (: mp (-> (: $maj (⟨->⟩ $P $Q))
                          (: $P ⟨wff⟩)
                          (: $Q ⟨wff⟩)
                          (: $min $P)
                          $Q)))


;; Assumption: ⟨t⟩ is a term (from the Hard test block).
!(add-atom &kbh (: ⟨t⟩ ⟨term⟩))

;; ---- Benchmark query: prove t = t at depth 5 (the th1 reflexivity proof) ----
;; Intended proof term (cross-engine contract / membership witness):
;;   (: (mp (mp (a1 (⟨+⟩ ⟨t⟩ ⟨0⟩) ⟨t⟩ ⟨t⟩) (⟨=⟩ (⟨+⟩ ⟨t⟩ ⟨0⟩) ⟨t⟩)
;;              (⟨->⟩ (⟨=⟩ (⟨+⟩ ⟨t⟩ ⟨0⟩) ⟨t⟩) (⟨=⟩ ⟨t⟩ ⟨t⟩)) (a2 ⟨t⟩))
;;          (⟨=⟩ (⟨+⟩ ⟨t⟩ ⟨0⟩) ⟨t⟩) (⟨=⟩ ⟨t⟩ ⟨t⟩) (a2 ⟨t⟩))
;;      (⟨=⟩ ⟨t⟩ ⟨t⟩))
!(bc &kbh (fromNumber 5) (: $prf (⟨=⟩ ⟨t⟩ ⟨t⟩)))
