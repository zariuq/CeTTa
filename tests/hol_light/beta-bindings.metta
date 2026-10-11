!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (beta-control:check $variable $body $type $expected)
  (hol-light:check-result
    (hol-light:run HOL:Nil
      (HOL:Cons (HOL:Step "beta" (BETA (Comb (Abs $variable $body) $variable))) HOL:Nil))
    "beta" HOL:Nil
    (Comb (Comb (Const "=" (hol-light:eq-type $type)) (Comb (Abs $variable $body) $variable)) $expected)))

!(BetaControl polymorphic
  (== (beta-control:check (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))
        (Tyvar "A") (Var "x" (Tyvar "A"))) True))
!(BetaControl nested-outer
  (== (beta-control:check (Var "x" (Tyvar "A"))
        (Abs (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A")))
        (hol-light:fun (Tyvar "A") (Tyvar "A"))
        (Abs (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A")))) True))
!(BetaControl inner-shadow
  (== (beta-control:check (Var "x" (Tyvar "A"))
        (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))
        (hol-light:fun (Tyvar "A") (Tyvar "A"))
        (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))) True))
!(BetaControl two-binders
  (== (beta-control:check (Var "x" (Tyvar "A"))
        (Abs (Var "y" (Tyvar "A")) (Abs (Var "z" (Tyvar "A")) (Var "x" (Tyvar "A"))))
        (hol-light:fun (Tyvar "A") (hol-light:fun (Tyvar "A") (Tyvar "A")))
        (Abs (Var "y" (Tyvar "A")) (Abs (Var "z" (Tyvar "A")) (Var "x" (Tyvar "A"))))) True))
!(BetaControl same-name-free-type
  (== (beta-control:check (Var "x" (Tyvar "A")) (Var "x" (Tyapp "bool" HOL:Nil))
        (Tyapp "bool" HOL:Nil) (Var "x" (Tyapp "bool" HOL:Nil))) True))
!(BetaControl capture-refused
  (== (beta-control:check (Var "x" (Tyvar "A"))
        (Abs (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A")))
        (hol-light:fun (Tyvar "A") (Tyvar "A"))
        (Abs (Var "y" (Tyvar "A")) (Var "y" (Tyvar "A")))) False))
!(BetaControl wrong-name
  (== (hol-light:infer (hol-light:initial HOL:Nil)
        (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "y" (Tyvar "A"))))) None))
!(BetaControl wrong-typed-identity
  (== (hol-light:infer (hol-light:initial HOL:Nil)
        (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "B"))))) None))
!(BetaControl nonvariable-argument
  (== (hol-light:infer (hol-light:initial HOL:Nil)
        (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))
          (Comb (Abs (Var "y" (Tyvar "A")) (Var "y" (Tyvar "A"))) (Var "x" (Tyvar "A")))))) None))
!(BetaControl closed-opening
  (== (hol-light:open-binder (HOL:Free "x" (Tyvar "A")) 0
        (HOL:Lam (Tyvar "A") (HOL:Bound 1 (Tyvar "A"))))
      (HOL:Lam (Tyvar "A") (HOL:Free "x" (Tyvar "A")))))
