; HOL Light theorems moved into Prime's own higher-order logic, and into the
; HOTG set theory over it with HOL Light's ind read as the sort of all sets.
; Every primitive HOL Light step becomes a theorem of the target that the
; target's own checker verifies; nothing HOL Light concluded is assumed.
; What the target must assume beyond its own logic is published as named
; commitments and listed.
(scope:profile lexical-fresh per-call reference)
!(import! &self ../../lib/prime/hol_light/data.metta)
!(import! &self ../../lib/prime/hol_light/syntax.metta)
!(import! &self ../../lib/prime/hol_light/kernel.metta)
!(import! &self ../../lib/prime/hol_light/definitions.metta)
!(import! &self ../../lib/prime/hol_light/derived.metta)
!(import! &self ../../lib/prime/hol_light/profile.metta)
!(import! &self ../../lib/prime/hol_light/transport.metta)

; Two target theory spaces: the logic alone, with a declared base type for
; ind, and the HOTG theory, where ind is read as set.
!(bind! &logic (let $s (new-space)
  (let $p (add-atom $s (hol:profile core))
    (let $r (add-atom $s (hol:rule hol-equality))
      (let $t (add-atom $s (: hl-ind (u 0))) $s)))))
!(bind! &sets (let $s (new-space)
  (let $p (add-atom $s (hol:profile megalodon-hotg))
    (let $r (add-atom $s (hol:rule hol-equality)) $s))))

(= (example:ind) (Tyapp "ind" HOL:Nil))
(= (example:bool) (Tyapp "bool" HOL:Nil))
(= (example:x) (Var "x" (Tyapp "ind" HOL:Nil)))
(= (example:f) (Var "f" (Tyapp "fun" (HOL:Cons (Tyapp "ind" HOL:Nil) (HOL:Cons (Tyapp "ind" HOL:Nil) HOL:Nil)))))
(= (example:p) (Var "p" (Tyapp "bool" HOL:Nil)))
(= (example:axiom $name)
  (case (hol-light:lookup $name (hol-light:standard-commitments)) (((Some $t) $t))))

; HOL Light derivations, checked by the hosted HOL Light kernel.
(= (example:commands)
  (let $sym (hol-light:sym-commands "sym-truth" (example:bool) (Const "T" (example:bool))
              "standard-definition:T")
    (hol-light:append $sym
      (HOL:Cons (HOL:Step "refl" (REFL (example:x)))
      (HOL:Cons (HOL:Step "refl-y" (REFL (Var "y" (Tyapp "ind" HOL:Nil))))
      (HOL:Cons (HOL:Step "beta" (BETA (Comb (Abs (example:x) (example:x)) (example:x))))
      (HOL:Cons (HOL:Step "trans" (TRANS "beta" "refl"))
      (HOL:Cons (HOL:Step "refl-f" (REFL (example:f)))
      (HOL:Cons (HOL:Step "congruence" (MK_COMB "refl-f" "trans"))
      (HOL:Cons (HOL:Step "abstraction" (ABS (example:x) "refl"))
      (HOL:Cons (HOL:Step "assume" (ASSUME (example:p)))
      (HOL:Cons (HOL:Step "antisymmetry" (DEDUCT_ANTISYM_RULE "assume" "assume"))
      (HOL:Cons (HOL:Step "refl-id" (REFL (Abs (example:p) (example:p))))
      (HOL:Cons (HOL:Step "truth" (EQ_MP "sym-truth" "refl-id"))
      (HOL:Cons (HOL:Step "refl-a" (REFL (Var "a" (Tyvar "A"))))
      (HOL:Cons (HOL:Step "refl-at-bool" (INST_TYPE (HOL:Cons (HOL:Maplet (example:bool) (Tyvar "A")) HOL:Nil) "refl-a"))
      (HOL:Cons (HOL:Step "select" (AXIOM "SELECT_AX" (example:axiom "SELECT_AX")))
      (HOL:Cons (HOL:Step "select-at-ind" (INST_TYPE (HOL:Cons (HOL:Maplet (example:ind) (Tyvar "A")) HOL:Nil) "select"))
      HOL:Nil))))))))))))))))))

; The kernel checks the derivations once; the state keeps every theorem with
; the request that produced it.
!(bind! &derivations (case (hol-light:standard-run (example:commands)) (((Some $s) $s))))

(= (example:logic $key)
  (let $state &derivations
    (hol-light:transport-replay &logic (HOL:Interpretation hl-ind) $state $key HOL:Nil)))
(= (example:sets $key)
  (let $state &derivations
    (hol-light:transport-replay &sets (HOL:Interpretation set) $state $key HOL:Nil)))

; Into the logic alone.
!(let $name (example:logic "congruence") (Congruence $name (hol:known-proposition &logic $name)))
!(let $name (example:logic "abstraction") (Abstraction $name (hol:known-proposition &logic $name)))
!(let $name (example:logic "antisymmetry") (Antisymmetry $name (hol:known-proposition &logic $name)))
!(let $name (example:logic "truth") (Truth $name (hol:known-proposition &logic $name)))
!(let $name (example:logic "refl-at-bool") (TypeInstance $name (hol:known-proposition &logic $name)))
!(let $name (example:logic "select-at-ind") (Choice $name))
; Free variables are generalized and named in order of occurrence: x = x and
; y = y, which HOL Light relates by INST, have one target statement.
!(let $x (example:logic "refl")
   (let $y (example:logic "refl-y")
     (let $a (hol:known-proposition &logic $x)
       (let $b (hol:known-proposition &logic $y) (SameStatement (== $a $b))))))
!(LogicCommitments (hol-light:transport-commitments &logic))
!(LogicDefinitions (collapse (match &logic (hol-light:transport-defined $d) $d)))

; Into the HOTG theory: the same derivations, now statements about sets.
!(let $name (example:sets "congruence") (SetCongruence $name (hol:known-proposition &sets $name)))
!(let $name (example:sets "antisymmetry") (SetAntisymmetry $name))
!(let $name (example:sets "truth") (SetTruth $name))
!(SetCommitments (hol-light:transport-commitments &sets))

; Negative control: a forged HOL Light theorem is not transported. The
; state's record for "refl" is replaced by x = y with the same request.
(= (example:forge-entries $entries)
  (case $entries
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons (HOL:Entry "refl" (HOL:Thm $h $c $d $o)) $rest)
       (HOL:Cons (HOL:Entry "refl" (HOL:Thm $h
         (HOL:App (HOL:App (HOL:C (HOL:Primitive "=") (hol-light:eq-type (example:ind)))
           (HOL:Free "x" (example:ind))) (HOL:Free "y" (example:ind))) $d $o))
         (example:forge-entries $rest)))
     ((HOL:Cons $entry $rest) (HOL:Cons $entry (example:forge-entries $rest))))))
(= (example:forged-state)
  (let (HOL:State $o $c $a $cache $r) &derivations
    (HOL:State $o $c $a (example:forge-entries $cache) $r)))
!(bind! &forged (let $s (new-space)
  (let $p (add-atom $s (hol:profile core))
    (let $r (add-atom $s (hol:rule hol-equality))
      (let $t (add-atom $s (: hl-ind (u 0))) $s)))))
!(let $state (example:forged-state)
   (case (hol-light:transport-replay &forged (HOL:Interpretation hl-ind) $state "refl" HOL:Nil)
     (((HOL:TransportRefused $key $request $why) (ForgedRefused $key (car-atom $why)))
      ($other (ForgedAccepted $other)))))

; Negative control: a transported step rests on its commitment. Withdrawing
; propositional extensionality invalidates the antisymmetry step.
!(let $name (example:logic "antisymmetry")
   (let $removed (match &logic (hol:known hl:propext $p $o $f $d $r $s)
                   (remove-atom &logic (hol:known hl:propext $p $o $f $d $r $s)))
     (let $verdict (try (hol:recheck &logic $name)) (WithoutPropext (car-atom $verdict)))))

; Negative control: a theorem checked in the logic alone is not cited in the
; HOTG space; a bridge must check it there.
!(let $name (example:logic "truth")
   (try (hol:proves &sets (pf:known $name) hl:T)))
