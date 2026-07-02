; Mine-chain focus benchmark, PeTTa import wrapper.
; Reports distinct target-gene counts, not proof-row multiplicities.

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

!(import! &self ../data/kb_eqtl_gene_links.metta)
!(import! &self ../data/kb_mined_patterns.metta)

(rule-sig dt (-> (: $e (eqtl $snp $gene))
                 (target $snp $gene)))

(rule-sig ct (-> (: $e1 (coreg $snp1 $snp2))
                 (: $e2 (target $snp2 $gene))
                 (target $snp1 $gene)))

(rule-sig xt (-> (: $e1 (target $snp $gene1))
                 (: $e2 (coexpr $gene1 $gene2))
                 (target $snp $gene2)))

(= (target-genes $snp $depth)
   (collapse
     (unique
       (let (: $proof (target $snp $gene))
            (bc &self (fromNumber $depth) (: $proof (target $snp $gene)))
         (target-gene $gene)))))

(= (focus-summary $snp)
   (let* (($direct (target-genes $snp 1))
          ($depth2 (target-genes $snp 2))
          ($direct-count (size-atom $direct))
          ($depth2-count (size-atom $depth2)))
     (mine-chain-focus $snp $direct-count $depth2-count (- $depth2-count $direct-count))))

!(focus-summary rs10000544)
!(focus-summary rs10000620)
!(focus-summary rs10000960)
