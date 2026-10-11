!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

; Ordinary MeTTa computes with HOL Light term data. Abs/Comb below describe
; HOL terms; let and let* are the surrounding MeTTa program's bindings.
; No runtime extension or foreign HOL-checking function is used.

(= (gift:var $name $type) (Var $name $type))
(= (gift:eq $type $left $right)
  (Comb (Comb (Const "=" (hol-light:eq-type $type)) $left) $right))

; Define polymorphic identity, derive I x = x, specialize its type to bool.
(= (gift:identity-commands)
  (let* (($A (Tyvar "A"))
         ($x (Var "x" $A))
         ($identity (Abs $x $x)))
    (HOL:Cons (HOL:Step "identity-definition"
      (DEFINITION "I" (hol-light:fun $A $A) $identity))
    (HOL:Cons (HOL:Step "argument" (REFL $x))
    (HOL:Cons (HOL:Step "application" (MK_COMB "identity-definition" "argument"))
    (HOL:Cons (HOL:Step "beta" (BETA (Comb $identity $x)))
    (HOL:Cons (HOL:Step "identity-law" (TRANS "application" "beta"))
    (HOL:Cons (HOL:Step "identity-at-bool"
      (INST_TYPE (HOL:Cons (HOL:Maplet (hol-light:bool) $A) HOL:Nil) "identity-law"))
      HOL:Nil))))))))

(= (gift:identity-goal $type)
  (let $x (Var "x" $type)
    (gift:eq $type (Comb (Const "I" (hol-light:fun $type $type)) $x) $x)))

!(let $commands (gift:identity-commands)
  (let $result (hol-light:run HOL:Nil $commands)
    (IdentityLaw
      (hol-light:check-result $result "identity-law" HOL:Nil
        (gift:identity-goal (Tyvar "A")))
      (SpecializedToBool
        (hol-light:check-result $result "identity-at-bool" HOL:Nil
          (gift:identity-goal (hol-light:bool))))
      (UsedAxioms (hol-light:dependencies $result "identity-at-bool"))
      (OrderedPremises (hol-light:premise-count $result "identity-law")))))

; A same-spelled free variable must remain free when two types merge.
(= (gift:capture-commands)
  (HOL:Cons (HOL:Step "before"
    (REFL (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "B")))))
  (HOL:Cons (HOL:Step "after"
    (INST_TYPE
      (HOL:Cons (HOL:Maplet (hol-light:bool) (Tyvar "A"))
      (HOL:Cons (HOL:Maplet (hol-light:bool) (Tyvar "B")) HOL:Nil)) "before"))
  HOL:Nil)))

!(let $commands (gift:capture-commands)
  (let $result (hol-light:run HOL:Nil $commands)
    (let* (($bool (hol-light:bool))
           ($safe (Abs (Var "fresh" $bool) (Var "x" $bool)))
           ($captured (Abs (Var "x" $bool) (Var "x" $bool))))
      (CaptureControl
        (FreeVariablePreserved
          (hol-light:check-result $result "after" HOL:Nil
            (gift:eq (hol-light:fun $bool $bool) $safe $safe)))
        (CapturedReadingAccepted
          (hol-light:check-result $result "after" HOL:Nil
            (gift:eq (hol-light:fun $bool $bool) $captured $captured)))))))

; This is an actual exported HOL Light proof closure, not an asserted name.
; It derives t = (t = T). Every received node's claimed theorem is checked.

(= (gift:source-events) (quote (HOL:Cons (HOL:CheckedReuse (HOL:Source 0) (DEFINITION "T" (Tyapp "bool" HOL:Nil) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil))))) "standard-definition:T" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "T" (Tyapp "bool" HOL:Nil))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 1) (REFL (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 2) (REFL (Const "T" (Tyapp "bool" HOL:Nil))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "T" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:CheckedStep (HOL:Source 3) (REFL (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))))) (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 4) (MK_COMB (HOL:Source 3) (HOL:Source 0)) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "T" (Tyapp "bool" HOL:Nil)))) (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil))))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 5) (MK_COMB (HOL:Source 4) (HOL:Source 2)) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "T" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil)))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil))))) (Const "T" (Tyapp "bool" HOL:Nil))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 6) (EQ_MP (HOL:Source 5) (HOL:Source 2)) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil)))) (Abs (Var "p" (Tyapp "bool" HOL:Nil)) (Var "p" (Tyapp "bool" HOL:Nil))))) (Const "T" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:CheckedStep (HOL:Source 7) (EQ_MP (HOL:Source 6) (HOL:Source 1)) HOL:Nil (Const "T" (Tyapp "bool" HOL:Nil))) (HOL:Cons (HOL:CheckedStep (HOL:Source 8) (ASSUME (Var "t" (Tyapp "bool" HOL:Nil))) (HOL:Cons (Var "t" (Tyapp "bool" HOL:Nil)) HOL:Nil) (Var "t" (Tyapp "bool" HOL:Nil))) (HOL:Cons (HOL:CheckedStep (HOL:Source 9) (DEDUCT_ANTISYM_RULE (HOL:Source 8) (HOL:Source 7)) (HOL:Cons (Var "t" (Tyapp "bool" HOL:Nil)) HOL:Nil) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:CheckedStep (HOL:Source 10) (ASSUME (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil))) HOL:Nil) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:CheckedStep (HOL:Source 11) (REFL (Var "t" (Tyapp "bool" HOL:Nil))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Var "t" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:CheckedStep (HOL:Source 12) (REFL (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil))))) (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 13) (MK_COMB (HOL:Source 12) (HOL:Source 10)) (HOL:Cons (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil))) HOL:Nil) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil)))) (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "T" (Tyapp "bool" HOL:Nil))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 14) (MK_COMB (HOL:Source 13) (HOL:Source 11)) (HOL:Cons (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil))) HOL:Nil) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Var "t" (Tyapp "bool" HOL:Nil)))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "T" (Tyapp "bool" HOL:Nil))) (Var "t" (Tyapp "bool" HOL:Nil))))) (HOL:Cons (HOL:CheckedStep (HOL:Source 15) (EQ_MP (HOL:Source 14) (HOL:Source 11)) (HOL:Cons (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil))) HOL:Nil) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Const "T" (Tyapp "bool" HOL:Nil))) (Var "t" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:CheckedStep (HOL:Source 16) (EQ_MP (HOL:Source 15) (HOL:Source 7)) (HOL:Cons (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil))) HOL:Nil) (Var "t" (Tyapp "bool" HOL:Nil))) (HOL:Cons (HOL:CheckedStep (HOL:Source 17) (DEDUCT_ANTISYM_RULE (HOL:Source 16) (HOL:Source 9)) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "t" (Tyapp "bool" HOL:Nil))) (Const "T" (Tyapp "bool" HOL:Nil))))) HOL:Nil))))))))))))))))))))


!(let $events (gift:source-events)
  (let $result (hol-light:standard-receive $events)
    (let* (($bool (hol-light:bool))
           ($t (Var "t" $bool))
           ($truth (Const "T" $bool)))
      (FromHOLLight
        (Received (hol-light:accepted $result))
        (EqualityWithTruth
          (hol-light:check-result $result (HOL:Source 17) HOL:Nil
            (gift:eq $bool $t (gift:eq $bool $t $truth))))
        (FalseClaimAccepted
          (hol-light:check-result $result (HOL:Source 17) HOL:Nil $t))
        (UsedAxioms (hol-light:dependencies $result (HOL:Source 17)))
        (Receipts (hol-light:receipt-count $result))))))

!(ForgedProofAccepted
  (hol-light:accepted
    (hol-light:run HOL:Nil
      (HOL:Cons (HOL:Step "forged"
        (HOL:Thm HOL:Nil (Var "p" (Tyapp "bool" HOL:Nil)) HOL:Nil CLAIM)) HOL:Nil))))
