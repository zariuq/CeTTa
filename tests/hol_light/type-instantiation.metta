!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (type-inst-control:eq $type $left $right)
  (Comb (Comb (Const "=" (hol-light:eq-type $type)) $left) $right))

(= (type-inst-control:check $term $entries $type $expected)
  (hol-light:check-result
    (hol-light:run HOL:Nil
      (HOL:Cons (HOL:Step "r" (REFL $term))
        (HOL:Cons (HOL:Step "i" (INST_TYPE $entries "r")) HOL:Nil)))
    "i" HOL:Nil (type-inst-control:eq $type $expected $expected)))

(= (type-inst-control:identity $type)
  (Abs (Var "x" $type) (Var "x" $type)))

(= (type-inst-control:predicate $type)
  (Comb (Var "p" (hol-light:fun $type (hol-light:bool))) (Var "x" $type)))

(= (type-inst-control:merge)
  (let $a (type-inst-control:predicate (Tyvar "A"))
    (let $b (type-inst-control:predicate (Tyvar "B"))
      (let $merged (type-inst-control:predicate (hol-light:bool))
        (hol-light:check-result
          (hol-light:run HOL:Nil
            (HOL:Cons (HOL:Step "a" (ASSUME $a))
              (HOL:Cons (HOL:Step "b" (ASSUME $b))
                (HOL:Cons (HOL:Step "d" (DEDUCT_ANTISYM_RULE "a" "b"))
                  (HOL:Cons (HOL:Step "i" (INST_TYPE
                    (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A"))
                      (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "B")) HOL:Nil)) "d")) HOL:Nil)))))
          "i" (HOL:Cons $merged HOL:Nil)
          (type-inst-control:eq (hol-light:bool) $merged $merged))))))

(= (type-inst-control:dependencies)
  (let $source (type-inst-control:eq (Tyvar "A") (Var "x" (Tyvar "A")) (Var "x" (Tyvar "A")))
    (let $result
      (hol-light:run (HOL:Cons (HOL:Entry "explicit" $source) HOL:Nil)
        (HOL:Cons (HOL:Step "a" (AXIOM "explicit" $source))
          (HOL:Cons (HOL:Step "i" (INST_TYPE
            (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil) "a")) HOL:Nil)))
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
                (and (== $origin (HOL:Origin "i" (INST_TYPE
                  (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil) "a")))
                  (hol-light:check-result $result "i" HOL:Nil
                    (type-inst-control:eq (hol-light:bool)
                      (Var "x" (hol-light:bool)) (Var "x" (hol-light:bool))))))))
             ($malformed False))))
         ($refused False))))))

!(TypeInstantiationControl polymorphic-identity
  (== (type-inst-control:check (type-inst-control:identity (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil)
        (hol-light:fun (hol-light:bool) (hol-light:bool))
        (Abs (Var "fresh" (hol-light:bool)) (Var "fresh" (hol-light:bool)))) True))
!(TypeInstantiationControl capture-avoided
  (== (type-inst-control:check (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "B")))
        (HOL:Cons (HOL:Maplet (Tyvar "B") (Tyvar "A")) HOL:Nil)
        (hol-light:fun (Tyvar "B") (Tyvar "B"))
        (Abs (Var "fresh" (Tyvar "B")) (Var "x" (Tyvar "B")))) True))
!(TypeInstantiationControl captured-claim-refused
  (== (type-inst-control:check (Abs (Var "x" (Tyvar "A")) (Var "x" (Tyvar "B")))
        (HOL:Cons (HOL:Maplet (Tyvar "B") (Tyvar "A")) HOL:Nil)
        (hol-light:fun (Tyvar "B") (Tyvar "B"))
        (Abs (Var "x" (Tyvar "B")) (Var "x" (Tyvar "B")))) False))
!(TypeInstantiationControl first-maplet
  (== (type-inst-control:check (Var "x" (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A"))
          (HOL:Cons (HOL:Maplet (Tyapp "ind" HOL:Nil) (Tyvar "A")) HOL:Nil))
        (hol-light:bool) (Var "x" (hol-light:bool))) True))
!(TypeInstantiationControl identity-shadows
  (== (type-inst-control:check (Var "x" (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Tyvar "A") (Tyvar "A"))
          (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil))
        (Tyvar "A") (Var "x" (Tyvar "A"))) True))
!(TypeInstantiationControl nonvariable-key-ignored
  (== (type-inst-control:check (Var "x" (Tyvar "A"))
        (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyapp "ind" HOL:Nil)) HOL:Nil)
        (Tyvar "A") (Var "x" (Tyvar "A"))) True))
!(TypeInstantiationControl empty-substitution
  (== (type-inst-control:check (Var "x" (Tyvar "A")) HOL:Nil
        (Tyvar "A") (Var "x" (Tyvar "A"))) True))
!(TypeInstantiationControl hypothesis-merge (== (type-inst-control:merge) True))
!(TypeInstantiationControl axiom-dependencies (== (type-inst-control:dependencies) True))
!(TypeInstantiationControl shadowed-invalid-maplet
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A"))))
          (HOL:Cons (HOL:Step "i" (INST_TYPE
            (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A"))
              (HOL:Cons (HOL:Maplet (Tyapp "fun" HOL:Nil) (Tyvar "A")) HOL:Nil)) "r")) HOL:Nil))) None))
!(TypeInstantiationControl ignored-invalid-key
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A"))))
          (HOL:Cons (HOL:Step "i" (INST_TYPE
            (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyapp "fun" HOL:Nil)) HOL:Nil) "r")) HOL:Nil))) None))
!(TypeInstantiationControl unresolved-key
  (== (hol-light:run HOL:Nil
        (HOL:Cons (HOL:Step "r" (REFL (Var "x" (Tyvar "A"))))
          (HOL:Cons (HOL:Step "i" (INST_TYPE
            (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyapp "undeclared" HOL:Nil)) HOL:Nil) "r")) HOL:Nil))) None))
!(TypeInstantiationControl missing-premise
  (== (hol-light:infer (hol-light:initial HOL:Nil)
        (INST_TYPE (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil) "missing")) None))
