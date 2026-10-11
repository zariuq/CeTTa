!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (cache-control:seed)
  (hol-light:run HOL:Nil
    (HOL:Cons (HOL:Step "seed" (REFL (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)))

(= (cache-control:equal $left $right)
  (Comb (Comb (Const "=" (hol-light:eq-type (hol-light:bool))) $left) $right))

(= (cache-control:claim $hypotheses $conclusion)
  (hol-light:check-result (cache-control:seed) "seed" $hypotheses $conclusion))

(= (cache-control:shared)
  (case (cache-control:seed)
    (((Some $state) (hol-light:step $state "shared" (TRANS "seed" "seed")))
     ($refused None))))

(= (cache-control:readback)
  (case (cache-control:seed)
    (((Some $state)
      (case (hol-light:known $state "seed")
        (((Some (HOL:Thm $hypotheses $conclusion $dependencies $origin))
          (and (== $hypotheses HOL:Nil)
            (and (== $dependencies HOL:Nil)
              (== $origin (HOL:Origin "seed" (REFL (Var "p" (Tyapp "bool" HOL:Nil))))))))
         ($malformed False))))
     ($refused False))))

(= (cache-control:shared-receipt)
  (case (cache-control:shared)
    (((Some (HOL:State $operators $constants $axioms $cache $receipts))
      (== $receipts
        (HOL:Cons (HOL:Receipt "shared" (TRANS "seed" "seed") (HOL:Cons "seed" (HOL:Cons "seed" HOL:Nil)))
          (HOL:Cons (HOL:Receipt "seed" (REFL (Var "p" (Tyapp "bool" HOL:Nil))) HOL:Nil) HOL:Nil))))
     ($refused False))))

(= (cache-control:shared-origin)
  (case (cache-control:shared)
    (((Some $state)
      (case ((hol-light:known $state "seed") (hol-light:known $state "shared"))
        ((((Some (HOL:Thm $h1 $c1 $d1 $o1)) (Some (HOL:Thm $h2 $c2 $d2 $o2)))
          (and (== $c1 $c2) (and (== $h1 $h2) (and (== $d1 $d2)
            (and (== (== $o1 $o2) False) (== $o2 (HOL:Origin "shared" (TRANS "seed" "seed"))))))))
         ($malformed False))))
     ($refused False))))

(= (cache-control:assumed)
  (hol-light:run HOL:Nil (HOL:Cons (HOL:Step "assumed" (ASSUME (Var "p" (Tyapp "bool" HOL:Nil)))) HOL:Nil)))

(= (cache-control:fresh-declaration)
  (case (cache-control:seed)
    (((Some $state)
      (case (hol-light:step $state "declared" (DECLARE_CONSTANT "unused" (Tyapp "bool" HOL:Nil)))
        (((Some $next) (== (hol-light:known $state "seed") (hol-light:known $next "seed")))
         ($refused False))))
     ($refused False))))

!(CacheAuthorizationControl earned-readback (== (cache-control:readback) True))
!(CacheAuthorizationControl earned-conclusion
  (== (cache-control:claim HOL:Nil (cache-control:equal (Var "p" (hol-light:bool)) (Var "p" (hol-light:bool)))) True))
!(CacheAuthorizationControl forged-conclusion
  (== (cache-control:claim HOL:Nil (Var "p" (hol-light:bool))) False))
!(CacheAuthorizationControl forged-hypothesis
  (== (cache-control:claim (HOL:Cons (Var "p" (hol-light:bool)) HOL:Nil)
    (cache-control:equal (Var "p" (hol-light:bool)) (Var "p" (hol-light:bool)))) False))
!(CacheAuthorizationControl shared-conclusion
  (== (hol-light:check-result (cache-control:shared) "shared" HOL:Nil
    (cache-control:equal (Var "p" (hol-light:bool)) (Var "p" (hol-light:bool)))) True))
!(CacheAuthorizationControl repeated-premise-receipt (== (cache-control:shared-receipt) True))
!(CacheAuthorizationControl same-fact-distinct-origin (== (cache-control:shared-origin) True))
!(CacheAuthorizationControl assumption-retained
  (== (hol-light:check-result (cache-control:assumed) "assumed"
    (HOL:Cons (Var "p" (hol-light:bool)) HOL:Nil) (Var "p" (hol-light:bool))) True))
!(CacheAuthorizationControl assumption-cannot-disappear
  (== (hol-light:check-result (cache-control:assumed) "assumed" HOL:Nil (Var "p" (hol-light:bool))) False))
!(CacheAuthorizationControl missing-key
  (== (hol-light:check-result (cache-control:seed) "missing" HOL:Nil (Var "p" (hol-light:bool))) False))
!(CacheAuthorizationControl fresh-declaration-retains-cache (== (cache-control:fresh-declaration) True))
