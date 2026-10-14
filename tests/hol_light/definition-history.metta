!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (definition-history:p) (Var "p" (hol-light:bool)))
(= (definition-history:A) (Tyvar "A"))
(= (definition-history:identity-type) (hol-light:fun (definition-history:A) (definition-history:A)))
(= (definition-history:identity)
  (Abs (Var "x" (definition-history:A)) (Var "x" (definition-history:A))))
(= (definition-history:before)
  (case (hol-light:step (hol-light:initial
          (HOL:Cons (HOL:Entry "selected" (definition-history:p)) HOL:Nil))
          "axiom-before" (AXIOM "selected" (definition-history:p)))
    (((Some $world)
      (case (hol-light:step $world "refl-before" (REFL (definition-history:p)))
        (((Some $proved) $proved) ($other malformed-reflexivity))))
     ($other malformed-axiom))))
(= (definition-history:define $world $key $name $schema $body)
  (hol-light:step $world $key (DEFINITION $name $schema $body)))
(= (definition-history:after)
  (case (definition-history:define (definition-history:before) "definition" "identity"
          (definition-history:identity-type) (definition-history:identity))
    (((Some $world) $world) ($other malformed-definition))))
(= (definition-history:has-state $result)
  (case $result (((Some (HOL:State $op $c $a $cache $receipts)) True) ($other False))))
(= (definition-history:reuse)
  (hol-light:step (definition-history:after) "reused" (EQ_MP "refl-before" "axiom-before")))
(= (definition-history:retained)
  (and (== (hol-light:known (definition-history:before) "axiom-before")
           (hol-light:known (definition-history:after) "axiom-before"))
       (== (hol-light:known (definition-history:before) "refl-before")
           (hol-light:known (definition-history:after) "refl-before"))))
(= (definition-history:defined-identity)
  (HOL:Defined "identity" (definition-history:identity-type)
    (HOL:Lam (definition-history:A) (HOL:Bound 0 (definition-history:A)))))
(= (definition-history:definition-record)
  (== (hol-light:known (definition-history:after) "definition")
    (hol-light:theorem-eq HOL:Nil
      (HOL:C (definition-history:defined-identity) (definition-history:identity-type))
      (HOL:Lam (definition-history:A) (HOL:Bound 0 (definition-history:A))) HOL:Nil
      (HOL:Origin "definition" (DEFINITION "identity"
        (definition-history:identity-type) (definition-history:identity))))))
(= (definition-history:receipt)
  (case (definition-history:after)
    (((HOL:State $op $c $a $cache $receipts)
      (case $receipts
        (((HOL:Cons (HOL:Receipt "definition" (DEFINITION "identity" $schema $body) HOL:Nil) $rest)
          (== (hol-light:length $rest) 2)) ($other False)))))))
(= (definition-history:hidden-world)
  (case (hol-light:step (hol-light:initial HOL:Nil) "declare-function"
          (DECLARE_CONSTANT "f" (hol-light:fun (definition-history:A) (hol-light:bool))))
    (((Some $first)
      (case (hol-light:step $first "declare-argument" (DECLARE_CONSTANT "x" (definition-history:A)))
        (((Some $second) $second) ($other malformed-argument))))
     ($other malformed-function))))
(= (definition-history:hidden-body)
  (Comb (Const "f" (hol-light:fun (definition-history:A) (hol-light:bool)))
       (Const "x" (definition-history:A))))
(= (definition-history:hidden-reading)
  (case (hol-light:read (definition-history:hidden-world) (definition-history:hidden-body))
    (((Some $body)
      (and (== (hol-light:term-type $body) (Some (hol-light:bool)))
           (== (hol-light:free-vars $body) HOL:Nil)))
     ($other False))))
(= (definition-history:hidden-annotations)
  (case (hol-light:read (definition-history:hidden-world) (definition-history:hidden-body))
    (((Some $body) (hol-light:member "A" (hol-light:term-type-vars $body)))
     ($other False))))

!(DefinitionHistoryControl accepted
  (== (definition-history:has-state (definition-history:define (definition-history:before)
    "definition" "identity" (definition-history:identity-type) (definition-history:identity))) True))
!(DefinitionHistoryControl old-evidence (== (definition-history:retained) True))
!(DefinitionHistoryControl reuse (== (definition-history:has-state (definition-history:reuse)) True))
!(DefinitionHistoryControl exact-definition (== (definition-history:definition-record) True))
!(DefinitionHistoryControl receipt (== (definition-history:receipt) True))
!(DefinitionHistoryControl polymorphic-instance
  (== (definition-history:has-state (hol-light:step (definition-history:after) "identity-bool"
    (REFL (Const "identity" (hol-light:fun (hol-light:bool) (hol-light:bool)))))) True))
!(DefinitionHistoryControl duplicate-name
  (== (definition-history:define (definition-history:after) "collision" "identity"
    (definition-history:identity-type) (definition-history:identity)) None))
!(DefinitionHistoryControl wrong-schema
  (== (definition-history:define (definition-history:before) "wrong" "identity"
    (hol-light:bool) (definition-history:identity)) None))
!(DefinitionHistoryControl free-variable
  (== (definition-history:define (definition-history:before) "free" "open"
    (hol-light:bool) (definition-history:p)) None))
!(DefinitionHistoryControl hidden-input-formed (== (definition-history:hidden-reading) True))
!(DefinitionHistoryControl hidden-input-parameter (== (definition-history:hidden-annotations) True))
!(DefinitionHistoryControl hidden-parameter
  (== (definition-history:define (definition-history:hidden-world) "hidden" "hidden"
    (hol-light:bool) (definition-history:hidden-body)) None))
!(DefinitionHistoryControl duplicate-event
  (== (definition-history:define (definition-history:after) "definition" "other"
    (definition-history:identity-type) (definition-history:identity)) None))
!(DefinitionHistoryControl definition-is-not-axiom
  (== (hol-light:step (definition-history:after) "unauthorized"
    (AXIOM "identity" (definition-history:p))) None))
