!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (term-inst-control:eq $type $left $right)
  (Comb (Comb (Const "=" (hol-light:eq-type $type)) $left) $right))

(= (term-inst-control:check $term $entries $type $expected)
  (hol-light:check-result
    (hol-light:run HOL:Nil
      (HOL:Cons (HOL:Step "r" (REFL $term))
        (HOL:Cons (HOL:Step "i" (INST $entries "r")) HOL:Nil)))
    "i" HOL:Nil (term-inst-control:eq $type $expected $expected)))

(= (term-inst-control:merge)
  (let $p (Var "p" (hol-light:bool))
    (let $q (Var "q" (hol-light:bool))
      (hol-light:check-result
        (hol-light:run HOL:Nil
          (HOL:Cons (HOL:Step "p" (ASSUME $p))
            (HOL:Cons (HOL:Step "q" (ASSUME $q))
              (HOL:Cons (HOL:Step "d" (DEDUCT_ANTISYM_RULE "p" "q"))
                (HOL:Cons (HOL:Step "i" (INST
                  (HOL:Cons (HOL:Maplet (Var "p" (Tyapp "bool" HOL:Nil))
                    (Var "q" (Tyapp "bool" HOL:Nil))) HOL:Nil) "d")) HOL:Nil)))))
        "i" (HOL:Cons $p HOL:Nil) (term-inst-control:eq (hol-light:bool) $p $p)))))

(= (term-inst-control:dependencies)
  (let $source (Var "p" (hol-light:bool))
    (let $result
      (hol-light:run (HOL:Cons (HOL:Entry "explicit" $source) HOL:Nil)
        (HOL:Cons (HOL:Step "a" (AXIOM "explicit" $source))
          (HOL:Cons (HOL:Step "i" (INST
            (HOL:Cons (HOL:Maplet (Var "q" (Tyapp "bool" HOL:Nil))
              (Var "p" (Tyapp "bool" HOL:Nil))) HOL:Nil) "a")) HOL:Nil)))
      (case $result
        (((Some $state)
          (case ((hol-light:known $state "a") (hol-light:known $state "i"))
            ((((Some (HOL:Thm $oldHyps $oldConclusion $oldDeps $oldOrigin))
                (Some (HOL:Thm $hyps $conclusion $deps $origin)))
              (and (== $oldDeps $deps)
                (and
                  (case $deps
                    (((HOL:Cons (HOL:AxiomDependency "explicit" $original) HOL:Nil)
                      (== (hol-light:read $state $source) (Some $original)))
                     ($wrongDependencies False)))
                  (and (== $origin (HOL:Origin "i" (INST
                    (HOL:Cons (HOL:Maplet (Var "q" (Tyapp "bool" HOL:Nil))
                      (Var "p" (Tyapp "bool" HOL:Nil))) HOL:Nil) "a")))
                    (hol-light:check-result $result "i" HOL:Nil (Var "q" (hol-light:bool)))))))
             ($malformed False))))
         ($refused False))))))

!(TermInstantiationControl simultaneous
  (== (term-inst-control:check (Var "x" (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) (HOL:Cons (HOL:Maplet (Var "z" (Tyvar "A")) (Var "y" (Tyvar "A"))) HOL:Nil))
        (Tyvar "A") (Var "y" (Tyvar "A"))) True))

!(TermInstantiationControl first-maplet
  (== (term-inst-control:check (Var "x" (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) (HOL:Cons (HOL:Maplet (Var "z" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil))
        (Tyvar "A") (Var "y" (Tyvar "A"))) True))

!(TermInstantiationControl identity-shadows
  (== (term-inst-control:check (Var "x" (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A"))) (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil))
        (Tyvar "A") (Var "x" (Tyvar "A"))) True))

!(TermInstantiationControl capture-avoided
  (== (term-inst-control:check (Abs (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A")))
        (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil)
        (hol-light:fun (Tyvar "A") (Tyvar "A")) (Abs (Var "fresh" (Tyvar "A")) (Var "y" (Tyvar "A")))) True))

!(TermInstantiationControl captured-claim-refused
  (== (term-inst-control:check (Abs (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A")))
        (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil)
        (hol-light:fun (Tyvar "A") (Tyvar "A")) (Abs (Var "y" (Tyvar "A")) (Var "y" (Tyvar "A")))) False))

!(TermInstantiationControl bound-unchanged
  (== (term-inst-control:check (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))
        (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil)
        (hol-light:fun (Tyvar "A") (Tyvar "A")) (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))) True))

!(TermInstantiationControl typed-key
  (== (term-inst-control:check (Var "x" (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "B")) (Var "x" (Tyvar "B"))) HOL:Nil)
        (Tyvar "A") (Var "x" (Tyvar "A"))) True))

!(TermInstantiationControl empty-substitution
  (== (term-inst-control:check (Var "x" (Tyvar "A"))
        HOL:Nil
        (Tyvar "A") (Var "x" (Tyvar "A"))) True))

!(TermInstantiationControl hypothesis-merge (== (term-inst-control:merge) True))
!(TermInstantiationControl axiom-dependencies (== (term-inst-control:dependencies) True))

!(TermInstantiationControl shadowed-invalid-maplet
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A"))))
          (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) (HOL:Cons (HOL:Maplet (Var "p" (Tyapp "bool" HOL:Nil)) (Var "x" (Tyvar "A"))) HOL:Nil)) "r")) HOL:Nil))) None))

!(TermInstantiationControl unused-invalid-maplet
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A"))))
          (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "unused" (Tyvar "B"))) HOL:Nil) "r")) HOL:Nil))) None))

!(TermInstantiationControl nonvariable-key
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A"))))
          (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))) HOL:Nil) "r")) HOL:Nil))) None))

!(TermInstantiationControl wrong-replacement-type
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A"))))
          (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "p" (Tyapp "bool" HOL:Nil)) (Var "x" (Tyvar "A"))) HOL:Nil) "r")) HOL:Nil))) None))

!(TermInstantiationControl missing-premise
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "i" (INST (HOL:Cons (HOL:Maplet (Var "y" (Tyvar "A")) (Var "x" (Tyvar "A"))) HOL:Nil) "missing")) HOL:Nil)) None))
