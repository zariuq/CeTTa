; he_prime recursive proof-search conformance.
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

(: Snp Type)
(: Gene Type)
(: rs1 Snp)
(: rs2 Snp)
(: gene1 Gene)

(: eqtl (-> Snp Gene Type))
(: target (-> Snp Gene Type))
(: coreg (-> Snp Snp Type))

(: eqtl_rs2_gene1 (eqtl rs2 gene1))
(: coreg_rs1_rs2 (coreg rs1 rs2))

(: dt (-> (: $e (eqtl $snp $gene)) (target $snp $gene)))
(: ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))

(= (target-proof Z $snp $gene)
   (let (: $e (eqtl $snp $gene))
      (match &self (: $e (eqtl $snp $gene)) (: $e (eqtl $snp $gene)))
      (dt $e)))

(= (target-proof (S $d) $snp1 $gene)
   (superpose
     ((target-proof Z $snp1 $gene)
      (let (: $e1 (coreg $snp1 $snp2))
         (match &self (: $e1 (coreg $snp1 $snp2)) (: $e1 (coreg $snp1 $snp2)))
         (let $sub (target-proof $d $snp2 $gene)
           (ct $e1 $sub))))))

!(assertEqualToResult
  (select (search-policy recursive-dependent-proof)
    (target-proof (S Z) rs1 gene1))
  ((ct coreg_rs1_rs2 (dt eqtl_rs2_gene1))))
!(assertEqual
  (get-type (ct coreg_rs1_rs2 (dt eqtl_rs2_gene1)))
  (target rs1 gene1))
