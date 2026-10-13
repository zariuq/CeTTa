!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

; REFL
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)))) "r" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) True)

; restricted BETA
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)))) "b" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A")))) True)

; ASSUME
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)))) "a" (HOL:Cons (Var "p" (Tyapp "bool" HOL:Nil)) HOL:Nil) (Var "p" (Tyapp "bool" HOL:Nil))) True)

; assumption cannot be omitted
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)))) "a" HOL:Nil (Var "p" (Tyapp "bool" HOL:Nil))) False)

; TRANS
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "t" (TRANS "b" "r")) HOL:Nil))))) "t" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A")))) True)

; TRANS middle mismatch
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "r2" (REFL (Var "y" (Tyvar "A")))) (HOL:Cons (HOL:Step "bad" (TRANS "r" "r2")) HOL:Nil)))))) "bad" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "y" (Tyvar "A")))) False)

; ABS alpha invariance
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "ab" (ABS (Var "x" (Tyvar "A")) "r")) HOL:Nil))))) "ab" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))) (Abs (Var "y" (Tyvar "A")) (Var "y" (Tyvar "A"))))) True)

; ABS hypothesis side condition
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "noabs" (ABS (Var "p" (Tyapp "bool" HOL:Nil)) "a")) HOL:Nil))))) "noabs" HOL:Nil (Var "p" (Tyapp "bool" HOL:Nil))) False)

; INST
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil) "b")) HOL:Nil))))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "y" (Tyvar "A")))) (Var "y" (Tyvar "A")))) True)

; DEDUCT_ANTISYM_RULE
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "d" (DEDUCT_ANTISYM_RULE "a" "a")) HOL:Nil))))) "d" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "p" (Tyapp "bool" HOL:Nil))) (Var "p" (Tyapp "bool" HOL:Nil)))) True)

; EQ_MP
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "b" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "a" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "rp" (REFL (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "mp" (EQ_MP "rp" "a")) HOL:Nil)))))) "mp" (HOL:Cons (Var "p" (Tyapp "bool" HOL:Nil)) HOL:Nil) (Var "p" (Tyapp "bool" HOL:Nil))) True)

; primitive BETA rejects a nontrivial argument
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "y" (Tyvar "A"))))) HOL:Nil)) None)

; ASSUME requires bool
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (ASSUME (Var "x" (Tyvar "A")))) HOL:Nil)) None)

; undeclared constant
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (REFL (Const "undeclared" (Tyvar "A")))) HOL:Nil)) None)

; forged theorem data
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "fake" (HOL:Thm HOL:Nil (Var "p" (Tyapp "bool" HOL:Nil)) HOL:Nil REFL)) HOL:Nil)) None)

; constant definition and identity law
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "def" (DEFINITION "I" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "rx" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "app" (MK_COMB "def" "rx")) (HOL:Cons (HOL:Step "beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "law" (TRANS "app" "beta")) HOL:Nil)))))) "law" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "I" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil)))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A")))) True)

; open constant definition
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (DEFINITION "bad" (Tyvar "A") (Var "x" (Tyvar "A")))) HOL:Nil)) None)

; duplicate constant definition
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "def" (DEFINITION "I" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "rx" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "app" (MK_COMB "def" "rx")) (HOL:Cons (HOL:Step "beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "law" (TRANS "app" "beta")) (HOL:Cons (HOL:Step "bad" (DEFINITION "I" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))))) HOL:Nil))))))) None)

; INST_TYPE specializes a polymorphic proof
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "i" (INST_TYPE (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyapp "bool" HOL:Nil))) (Var "x" (Tyapp "bool" HOL:Nil)))) True)

; INST_TYPE preserves a newly same-typed free name
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "B"))))) (HOL:Cons (HOL:Step "i" (INST_TYPE (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "B")) HOL:Nil)) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "fresh" (Tyapp "bool" HOL:Nil)) (Var "x" (Tyapp "bool" HOL:Nil)))) (Abs (Var "fresh" (Tyapp "bool" HOL:Nil)) (Var "x" (Tyapp "bool" HOL:Nil))))) True)

; INST_TYPE captured reading is refused
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "B"))))) (HOL:Cons (HOL:Step "i" (INST_TYPE (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "B")) HOL:Nil)) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "x" (Tyapp "bool" HOL:Nil)) (Var "x" (Tyapp "bool" HOL:Nil)))) (Abs (Var "x" (Tyapp "bool" HOL:Nil)) (Var "x" (Tyapp "bool" HOL:Nil))))) False)

; INST leaves replacements free under a binder
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Abs (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "z" (Tyvar "A")) (Var "y" (Tyvar "A")))) (Abs (Var "z" (Tyvar "A")) (Var "y" (Tyvar "A"))))) True)

; INST captured reading is refused
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Abs (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil))) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Abs (Var "y" (Tyvar "A")) (Var "y" (Tyvar "A")))) (Abs (Var "y" (Tyvar "A")) (Var "y" (Tyvar "A"))))) False)

; INST uses the first matching pair
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) (HOL:Cons (HOL:Maplet (Var "z" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil)) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "y" (Tyvar "A"))) (Var "y" (Tyvar "A")))) True)

; INST_TYPE uses the first matching pair
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "i" (INST_TYPE (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) (HOL:Cons (HOL:Maplet (Tyvar "B") (Tyvar "A")) HOL:Nil)) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyapp "bool" HOL:Nil))) (Var "x" (Tyapp "bool" HOL:Nil)))) True)

; INST_TYPE ignores a non-variable key as in the source
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "i" (INST_TYPE (HOL:Cons (HOL:Maplet (Tyvar "B") (Tyapp "bool" HOL:Nil)) HOL:Nil) "r")) HOL:Nil))) "i" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) True)

; INST rejects a non-variable term key
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "x" (Tyvar "A")) (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))) HOL:Nil) "r")) HOL:Nil))) None)

; INST rejects a wrong replacement type
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "p" (Tyapp "bool" HOL:Nil)) (Var "x" (Tyvar "A"))) HOL:Nil) "r")) HOL:Nil))) None)

; MK_COMB checks both applications
!(test (hol-light:check-result (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "f" (REFL (Var "f" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil)))))) (HOL:Cons (HOL:Step "x" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "a" (MK_COMB "f" "x")) HOL:Nil)))) "a" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Var "f" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil)))) (Var "x" (Tyvar "A")))) (Comb (Var "f" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil)))) (Var "x" (Tyvar "A"))))) True)

; MK_COMB rejects a wrong argument type
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "f" (REFL (Var "f" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyvar "A") HOL:Nil)))))) (HOL:Cons (HOL:Step "p" (REFL (Var "p" (Tyapp "bool" HOL:Nil)))) (HOL:Cons (HOL:Step "a" (MK_COMB "f" "p")) HOL:Nil)))) None)

; dependencies must have been checked earlier
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "a" (TRANS "later" "later")) (HOL:Cons (HOL:Step "later" (REFL (Var "x" (Tyvar "A")))) HOL:Nil))) None)

; proof ids are unique
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "r" (REFL (Var "y" (Tyvar "A")))) HOL:Nil))) None)

; declaration ids are unique too
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "same" (DECLARE_TYPE "alpha" 0)) (HOL:Cons (HOL:Step "same" (DECLARE_TYPE "beta" 0)) HOL:Nil))) None)

; malformed type-operator arity
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (DECLARE_TYPE "alpha" 1.5)) HOL:Nil)) None)

; malformed type application
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (REFL (Var "x" (Tyapp "fun" (HOL:Cons (Tyvar "A") HOL:Nil))))) HOL:Nil)) None)

; refusal is sticky
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (ASSUME (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "good" (REFL (Var "x" (Tyvar "A")))) HOL:Nil))) None)

; raw open commands are refused
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (REFL $open)) HOL:Nil)) None)

; repeated premise occurrences remain in the receipt
!(test (hol-light:premise-count (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "t" (TRANS "r" "r")) HOL:Nil))) "t") 2)

; a proof without axioms records no commitments
!(test (hol-light:dependencies (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "t" (TRANS "r" "r")) HOL:Nil))) "t") HOL:Nil)

; separately selected axiom
!(test (hol-light:check-result (hol-light:run (HOL:Cons (HOL:Entry "guest-p" (Var "p" (Tyapp "bool" HOL:Nil))) HOL:Nil) (HOL:Cons (HOL:Step "a" (AXIOM "guest-p" (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)) "a" HOL:Nil (Var "p" (Tyapp "bool" HOL:Nil))) True)

; unselected axiom is refused
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "a" (AXIOM "guest-p" (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)) None)

; a selected name cannot authorize a different formula
!(test (hol-light:run (HOL:Cons (HOL:Entry "guest-p" (Var "p" (Tyapp "bool" HOL:Nil))) HOL:Nil) (HOL:Cons (HOL:Step "a" (AXIOM "guest-p" (Var "q" (Tyapp "bool" HOL:Nil)))) HOL:Nil)) None)

; axiom provenance retains the formula
!(test (hol-light:length (hol-light:dependencies (hol-light:run (HOL:Cons (HOL:Entry "guest-p" (Var "p" (Tyapp "bool" HOL:Nil))) HOL:Nil) (HOL:Cons (HOL:Step "a" (AXIOM "guest-p" (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)) "a")) 1)

; type definition returns the abstraction law
!(test (hol-light:check-result (let $commands (hol-light:append (hol-light:append (HOL:Cons (HOL:Step "witness-refl" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "predicate-beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) HOL:Nil)) (hol-light:sym-commands "predicate-back" (Tyapp "bool" HOL:Nil) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))) "predicate-beta")) (HOL:Cons (HOL:Step "inhabited" (EQ_MP "predicate-back" "witness-refl")) (HOL:Cons (HOL:Step "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "inhabited")) HOL:Nil))) (hol-light:run HOL:Nil $commands)) "type" HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "abs-copy" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) HOL:Nil)))) (Comb (Const "rep-copy" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)))))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil))))) True)

; type definition returns the representation law
!(test (hol-light:check-result (let $commands (hol-light:append (hol-light:append (HOL:Cons (HOL:Step "witness-refl" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "predicate-beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) HOL:Nil)) (hol-light:sym-commands "predicate-back" (Tyapp "bool" HOL:Nil) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))) "predicate-beta")) (HOL:Cons (HOL:Step "inhabited" (EQ_MP "predicate-back" "witness-refl")) (HOL:Cons (HOL:Step "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "inhabited")) HOL:Nil))) (hol-light:run HOL:Nil $commands)) (HOL:RepKey "type") HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "r" (Tyvar "A")))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "rep-copy" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Comb (Const "abs-copy" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) HOL:Nil)))) (Var "r" (Tyvar "A"))))) (Var "r" (Tyvar "A"))))) True)

; type nonemptiness proof is retained as a premise
!(test (hol-light:premise-count (let $commands (hol-light:append (hol-light:append (HOL:Cons (HOL:Step "witness-refl" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "predicate-beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) HOL:Nil)) (hol-light:sym-commands "predicate-back" (Tyapp "bool" HOL:Nil) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))) "predicate-beta")) (HOL:Cons (HOL:Step "inhabited" (EQ_MP "predicate-back" "witness-refl")) (HOL:Cons (HOL:Step "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "inhabited")) HOL:Nil))) (hol-light:run HOL:Nil $commands)) "type") 1)

; received type definition checks both laws
!(test (let $commands (hol-light:append (hol-light:append (HOL:Cons (HOL:Step "witness-refl" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "predicate-beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) HOL:Nil)) (hol-light:sym-commands "predicate-back" (Tyapp "bool" HOL:Nil) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))) "predicate-beta")) (HOL:Cons (HOL:Step "inhabited" (EQ_MP "predicate-back" "witness-refl")) HOL:Nil)) (case (hol-light:run HOL:Nil $commands) (( (Some $state) (hol-light:accepted (hol-light:receive-from $state (HOL:Cons (HOL:CheckedType "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "inhabited") HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "abs-copy" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) HOL:Nil)))) (Comb (Const "rep-copy" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)))))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "bool" HOL:Nil) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "r" (Tyvar "A")))) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "rep-copy" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Comb (Const "abs-copy" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) HOL:Nil)))) (Var "r" (Tyvar "A"))))) (Var "r" (Tyvar "A"))))) HOL:Nil)))) ( $refused False)))) True)

; a false second type-definition law is refused
!(test (let $commands (hol-light:append (hol-light:append (HOL:Cons (HOL:Step "witness-refl" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "predicate-beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) HOL:Nil)) (hol-light:sym-commands "predicate-back" (Tyapp "bool" HOL:Nil) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))) "predicate-beta")) (HOL:Cons (HOL:Step "inhabited" (EQ_MP "predicate-back" "witness-refl")) HOL:Nil)) (case (hol-light:run HOL:Nil $commands) (( (Some $state) (hol-light:accepted (hol-light:receive-from $state (HOL:Cons (HOL:CheckedType "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "inhabited") HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "abs-copy" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) HOL:Nil)))) (Comb (Const "rep-copy" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)))))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)))) HOL:Nil (Var "p" (Tyapp "bool" HOL:Nil))) HOL:Nil)))) ( $refused False)))) False)

; a type definition cannot omit its second result
!(test (let $commands (hol-light:append (hol-light:append (HOL:Cons (HOL:Step "witness-refl" (REFL (Var "x" (Tyvar "A")))) (HOL:Cons (HOL:Step "predicate-beta" (BETA (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) HOL:Nil)) (hol-light:sym-commands "predicate-back" (Tyapp "bool" HOL:Nil) (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))) "predicate-beta")) (HOL:Cons (HOL:Step "inhabited" (EQ_MP "predicate-back" "witness-refl")) HOL:Nil)) (case (hol-light:run HOL:Nil $commands) (( (Some $state) (hol-light:accepted (hol-light:receive-from $state (HOL:Cons (HOL:CheckedStep "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "inhabited") HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "abs-copy" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) HOL:Nil)))) (Comb (Const "rep-copy" (Tyapp "fun" (HOL:Cons (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil)))))) (Var "a" (Tyapp "copy" (HOL:Cons (Tyvar "A") HOL:Nil))))) HOL:Nil)))) ( $refused False)))) False)

; an assumption is not a closed nonemptiness proof
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "a" (ASSUME (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "a")) HOL:Nil))) None)

; a type predicate may not have a free term parameter
!(test (hol-light:run (HOL:Cons (HOL:Entry "open" (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "y" (Tyvar "A")))) (Var "x" (Tyvar "A")))) HOL:Nil) (HOL:Cons (HOL:Step "a" (AXIOM "open" (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "y" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "type" (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "a")) HOL:Nil))) None)

; a missing witness cannot define an empty carrier
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "type" (TYPE_DEFINITION "empty" "abs-empty" "rep-empty" "missing")) HOL:Nil)) None)

; abstraction and representation names must differ
!(test (hol-light:run (HOL:Cons (HOL:Entry "p" (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A")))) HOL:Nil) (HOL:Cons (HOL:Step "a" (AXIOM "p" (Comb (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) (Var "x" (Tyvar "A"))))) (HOL:Cons (HOL:Step "type" (TYPE_DEFINITION "copy" "same" "same" "a")) HOL:Nil))) None)

; a definition cannot hide a type variable absent from its schema
!(test (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "bad" (DEFINITION "hidden" (Tyapp "bool" HOL:Nil) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Comb (Const "@" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))))) (Comb (Const "@" (Tyapp "fun" (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) (HOL:Cons (Tyvar "A") HOL:Nil)))) (Abs (Var "x" (Tyvar "A")) (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))))))) HOL:Nil)) None)

; receiving checks an actual derived theorem
!(test (hol-light:accepted (hol-light:receive HOL:Nil (HOL:Cons (HOL:CheckedStep "r" (REFL (Var "x" (Tyvar "A"))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "x" (Tyvar "A")))) HOL:Nil))) True)

; receiving refuses a false exported conclusion
!(test (hol-light:receive HOL:Nil (HOL:Cons (HOL:CheckedStep "r" (REFL (Var "x" (Tyvar "A"))) HOL:Nil (Comb (Comb (Const "=" (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "fun" (HOL:Cons (Tyvar "A") (HOL:Cons (Tyapp "bool" HOL:Nil) HOL:Nil))) HOL:Nil)))) (Var "x" (Tyvar "A"))) (Var "y" (Tyvar "A")))) HOL:Nil)) None)

; Standard profile selection is explicit.
!(test (let $axioms (hol-light:standard-commitments)
  (case (hol-light:lookup "ETA_AX" $axioms)
    (((Some $formula)
      (let $commands (HOL:Cons (HOL:Step "eta" (AXIOM "ETA_AX" $formula)) HOL:Nil)
        (hol-light:accepted (hol-light:standard-run $commands))))
     ($bad False)))) True)

; A type definition's second result cannot reuse a declaration's receipt key.
!(test
  (let* (($type (Tyvar "A"))
         ($x (Var "x" $type))
         ($predicate (Abs $x
           (Comb (Comb (Const "=" (hol-light:eq-type $type)) $x) $x)))
         ($inhabited (Comb $predicate $x))
         ($commitments (HOL:Cons (HOL:Entry "witness" $inhabited) HOL:Nil))
         ($commands
           (HOL:Cons (HOL:Step (HOL:RepKey "copy") (DECLARE_TYPE "spare" 0))
           (HOL:Cons (HOL:Step "witness" (AXIOM "witness" $inhabited))
           (HOL:Cons (HOL:Step "copy"
             (TYPE_DEFINITION "copy" "abs-copy" "rep-copy" "witness")) HOL:Nil)))))
    (hol-light:accepted (hol-light:run $commitments $commands)))
  False)
