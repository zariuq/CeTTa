; Variant tabling should be observably active on a bio-style equation query.
;
; Positive example:
; repeated bio-drug-route queries reuse a staged variant entry.
;
; Negative example:
; variant mode is enabled, but repeated equation calls still behave like cold misses.

!(pragma! search-table-mode variant)

; ---- Nat -------------------------------------------------------------------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; ---- nilbc -----------------------------------------------------------------
(: bc (-> $a Nat $b $b))
(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))
(= (bc $kb $_ (rule-sig $name $type))
   (match $kb (rule-sig $name $type) (rule-sig $name $type)))

(= (bc $kb $_ (: (eqtl-fact $snp $gene) (eqtl $snp $gene)))
   (match $kb (eqtl-link $snp $gene)
     (: (eqtl-fact $snp $gene) (eqtl $snp $gene))))
(= (bc $kb $_ (: (disease-fact $gene $disease) (has-disease $gene $disease)))
   (match $kb (disease-gene $disease $gene)
     (: (disease-fact $gene $disease) (has-disease $gene $disease))))
(= (bc $kb $_ (: (treats-fact $compound $disease) (treats $compound $disease)))
   (match $kb (compound-treats $compound $disease)
     (: (treats-fact $compound $disease) (treats $compound $disease))))

(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm))
           (bc $kb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))
           (bc $kb $d (rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1)
           (bc $kb $d (: $p1 $t1)))
          ((: $p2 $t2)
           (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

; ---- Tiny bio-style workload -----------------------------------------------
(rule-sig dt (-> (: $e (eqtl $snp $gene))
                 (target $snp $gene)))
(rule-sig dg (-> (: $e1 (target $snp $gene))
                 (: $e2 (has-disease $gene $disease))
                 (disease-reach $snp $disease $gene)))
(rule-sig td (-> (: $e1 (disease-reach $snp $disease $gene))
                 (: $e2 (treats $compound $disease))
                 (drug-hypothesis $snp $compound $disease $gene)))

(eqtl-link rs10000544 ensg00000138735)
(disease-gene disease-alpha ensg00000138735)
(compound-treats compound-omega disease-alpha)

(= (bio-drug-route $snp $compound $disease $gene)
   (let (: $proof (drug-hypothesis $snp $compound $disease $gene))
        (bc &self (fromNumber 3)
            (: $proof (drug-hypothesis $snp $compound $disease $gene)))
     (drug-route $compound $disease $gene)))

!(assertEqualToResult
  (bio-drug-route rs10000544 $compound $disease $gene)
  ((drug-route compound-omega disease-alpha ensg00000138735)))

!(reset-runtime-stats!)
!(assertEqualToResult
  (bio-drug-route rs10000544 $compound $disease $gene)
  ((drug-route compound-omega disease-alpha ensg00000138735)))
!(assertEqualToResult
  (bio-drug-route rs10000544 $compound $disease $gene)
  ((drug-route compound-omega disease-alpha ensg00000138735)))

!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats (runtime-counter table-miss $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter table-hit $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter table-answer-staged $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter table-stale-miss $n) $n)
  (0))
