; Focused mined-pattern backward-chaining probe for the benchmark audit.
; Same semantics as reports/.../mine_chain_focus_20260627.metta, but placed
; inside the CeTTa repo tree so CeTTa can resolve the test-data imports using
; the same relative pattern as shipped benchmarks.

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(: bc (-> $a Nat $b $b))

(= (bc $kb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))

(= (bc $kb $_ (rule-sig $name $type))
   (match $kb (rule-sig $name $type) (rule-sig $name $type)))

(= (bc $kb $_ (: (eqtl-fact $snp $gene) (eqtl $snp $gene)))
   (match $kb (eqtl-link $snp $gene)
     (: (eqtl-fact $snp $gene) (eqtl $snp $gene))))

(= (bc $kb $_ (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2)))
   (match $kb (co-regulate $s1 $s2 $surp)
     (: (coreg-fact $s1 $s2 $surp) (coreg $s1 $s2))))

(= (bc $kb $_ (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2)))
   (match $kb (co-express $g1 $g2 $surp)
     (: (coexpr-fact $g1 $g2 $surp) (coexpr $g1 $g2))))

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

!(import! &self ./data/kb_eqtl_gene_links.metta)
!(import! &self ./data/kb_mined_patterns.metta)

(rule-sig dt (-> (: $e (eqtl $snp $gene))
                 (target $snp $gene)))

(rule-sig ct (-> (: $e1 (coreg $snp1 $snp2))
                 (: $e2 (target $snp2 $gene))
                 (target $snp1 $gene)))

(rule-sig xt (-> (: $e1 (target $snp $gene1))
                 (: $e2 (coexpr $gene1 $gene2))
                 (target $snp $gene2)))

!(collapse
  (unique
    (let (: $proof (target rs10000544 $gene))
         (bc &self (fromNumber 1) (: $proof (target rs10000544 $gene)))
      (direct-target rs10000544 $gene $proof))))

!(collapse
  (unique
    (let (: $proof (target rs10000544 $gene))
         (bc &self (fromNumber 2) (: $proof (target rs10000544 $gene)))
      (depth2-target rs10000544 $gene $proof))))

!(collapse
  (unique
    (let (: $proof (target rs10000620 $gene))
         (bc &self (fromNumber 1) (: $proof (target rs10000620 $gene)))
      (direct-target rs10000620 $gene $proof))))

!(collapse
  (unique
    (let (: $proof (target rs10000620 $gene))
         (bc &self (fromNumber 2) (: $proof (target rs10000620 $gene)))
      (depth2-target rs10000620 $gene $proof))))

!(collapse
  (unique
    (let (: $proof (target rs10000960 $gene))
         (bc &self (fromNumber 1) (: $proof (target rs10000960 $gene)))
      (direct-target rs10000960 $gene $proof))))

!(collapse
  (unique
    (let (: $proof (target rs10000960 $gene))
         (bc &self (fromNumber 2) (: $proof (target rs10000960 $gene)))
      (depth2-target rs10000960 $gene $proof))))
