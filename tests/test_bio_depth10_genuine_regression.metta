; Genuine depth-10 backward chaining regression.
; The final theorem has exactly one derivation spine of 10 rule applications.
; Depth 9 must fail; depth 10 must succeed.

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(: bc (-> $a Nat $b $b))
(= (bc $kb $_ (: $prf $thm)) (match $kb (: $prf $thm) (: $prf $thm)))
(= (bc $kb $_ (rule-sig $name $type)) (match $kb (rule-sig $name $type) (rule-sig $name $type)))

(= (bc $kb $_ (: (eqtl-fact $snp $gene) (eqtl $snp $gene)))
   (match $kb (eqtl-link $snp $gene)
     (: (eqtl-fact $snp $gene) (eqtl $snp $gene))))
(= (bc $kb $_ (: (tissue-fact $gene $tissue) (has-tissue $gene $tissue)))
   (match $kb (tissue-class $gene $tissue)
     (: (tissue-fact $gene $tissue) (has-tissue $gene $tissue))))
(= (bc $kb $_ (: (pw-fact $gene $pw) (in-pw $gene $pw)))
   (match $kb (in-pathway $gene $pw)
     (: (pw-fact $gene $pw) (in-pw $gene $pw))))
(= (bc $kb $_ (: (disease-fact $gene $disease) (has-disease $gene $disease)))
   (match $kb (disease-gene $disease $gene)
     (: (disease-fact $gene $disease) (has-disease $gene $disease))))
(= (bc $kb $_ (: (treats-fact $comp $disease) (treats $comp $disease)))
   (match $kb (compound-treats $comp $disease)
     (: (treats-fact $comp $disease) (treats $comp $disease))))
(= (bc $kb $_ (: (binds-fact $comp $gene) (binds $comp $gene)))
   (match $kb (compound-binds $comp $gene)
     (: (binds-fact $comp $gene) (binds $comp $gene))))
(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm)) (bc $kb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1) (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm)) (bc $kb $d (rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1) (bc $kb $d (: $p1 $t1)))
          ((: $p2 $t2) (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

; Base facts
(: rsA Atom)
(: gSeed Atom)
(: gNeighbor Atom)
(: gBound Atom)
(: tissue-broad Atom)
(: pwA Atom)
(: diseaseA Atom)
(: compA Atom)
(: diseaseB Atom)
(: compB Atom)
(: pwB Atom)

(eqtl-link rsA gSeed)
(tissue-class gSeed tissue-broad)
(in-pathway gSeed pwA)
(in-pathway gNeighbor pwA)
(disease-gene diseaseA gNeighbor)
(compound-treats compA diseaseA)
(compound-binds compA gBound)
(disease-gene diseaseB gBound)
(compound-treats compB diseaseB)
(in-pathway gBound pwB)

; 10-rule spine
(rule-sig r1-target
  (-> (: $e (eqtl $snp $seed))
      (target $snp $seed)))

(rule-sig r2-tissue
  (-> (: $e1 (target $snp $seed))
      (: $e2 (has-tissue $seed $tissue))
      (tissue-seed $snp $seed $tissue)))

(rule-sig r3-pathway
  (-> (: $e1 (tissue-seed $snp $seed $tissue))
      (: $e2 (in-pw $seed $pw))
      (pathway-seed $snp $seed $tissue $pw)))

(rule-sig r4-hop
  (-> (: $e1 (pathway-seed $snp $seed $tissue $pw))
      (: $e2 (in-pw $neighbor $pw))
      (pathway-hop $snp $seed $neighbor $tissue $pw)))

(rule-sig r5-disease
  (-> (: $e1 (pathway-hop $snp $seed $neighbor $tissue $pw))
      (: $e2 (has-disease $neighbor $disease))
      (disease-hop $snp $seed $neighbor $disease $tissue $pw)))

(rule-sig r6-treats
  (-> (: $e1 (disease-hop $snp $seed $neighbor $disease $tissue $pw))
      (: $e2 (treats $comp $disease))
      (treated-hop $snp $seed $neighbor $disease $comp $tissue $pw)))

(rule-sig r7-binds
  (-> (: $e1 (treated-hop $snp $seed $neighbor $disease $comp $tissue $pw))
      (: $e2 (binds $comp $bound))
      (bound-hop $snp $seed $neighbor $disease $comp $bound $tissue $pw)))

(rule-sig r8-bound-disease
  (-> (: $e1 (bound-hop $snp $seed $neighbor $disease $comp $bound $tissue $pw))
      (: $e2 (has-disease $bound $disease2))
      (bound-disease-hop $snp $seed $neighbor $disease $comp $bound $disease2 $tissue $pw)))

(rule-sig r9-rebound-drug
  (-> (: $e1 (bound-disease-hop $snp $seed $neighbor $disease $comp $bound $disease2 $tissue $pw))
      (: $e2 (treats $comp2 $disease2))
      (rebound-drug-hop $snp $seed $neighbor $disease $comp $bound $disease2 $comp2 $tissue $pw)))

(rule-sig r10-rebound
  (-> (: $e1 (rebound-drug-hop $snp $seed $neighbor $disease $comp $bound $disease2 $comp2 $tissue $pw))
      (: $e2 (in-pw $bound $pw2))
      (rebound-hypothesis $snp $comp $comp2 $disease $disease2 $seed $neighbor $bound $tissue $pw $pw2)))

!(let $hits
      (collapse
        (let (: $proof (rebound-hypothesis rsA compA compB diseaseA diseaseB gSeed gNeighbor gBound tissue-broad pwA pwB))
             (bc &self (fromNumber 9)
                 (: $proof (rebound-hypothesis rsA compA compB diseaseA diseaseB gSeed gNeighbor gBound tissue-broad pwA pwB)))
          hit))
   (depth-9 (size-atom $hits)))

!(let $hits
      (collapse
        (let (: $proof (rebound-hypothesis rsA compA compB diseaseA diseaseB gSeed gNeighbor gBound tissue-broad pwA pwB))
             (bc &self (fromNumber 10)
                 (: $proof (rebound-hypothesis rsA compA compB diseaseA diseaseB gSeed gNeighbor gBound tissue-broad pwA pwB)))
          hit))
   (depth-10 (size-atom $hits)))
