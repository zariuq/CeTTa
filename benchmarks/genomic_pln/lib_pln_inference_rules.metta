; Shared WM-PLN workload rules for realistic genomic hypothesis generation.
; Positive example:
; many biologically distinct routes compete to derive the same disease and drug
; hypotheses.
;
; Negative example:
; one decorative chain whose topology is fixed in advance.

!(import! &self lib_wmpln)

; ---- WM scoring helpers ----------------------------------------------------

(= (wm-base-rate) 0.084)

(= (wm-posterior-mean (stv $s $c))
   (+ (* $s $c) (* (- 1 $c) (wm-base-rate))))

(= (proof-score $proof)
   (wm-posterior-mean (proof-stv $proof)))

; ---- Leaf truth values -----------------------------------------------------

(= (eqtl-stv effect-strong tissue-broad)      (stv 0.90 0.90))
(= (eqtl-stv effect-strong tissue-moderate)   (stv 0.85 0.82))
(= (eqtl-stv effect-strong tissue-rare)       (stv 0.78 0.72))
(= (eqtl-stv effect-moderate tissue-broad)    (stv 0.74 0.84))
(= (eqtl-stv effect-moderate tissue-moderate) (stv 0.68 0.76))
(= (eqtl-stv effect-moderate tissue-rare)     (stv 0.58 0.66))
(= (eqtl-stv effect-weak tissue-broad)        (stv 0.55 0.74))
(= (eqtl-stv effect-weak tissue-moderate)     (stv 0.46 0.64))
(= (eqtl-stv effect-weak tissue-rare)         (stv 0.35 0.56))

(= (tissue-stv tissue-broad)    (stv 0.82 0.82))
(= (tissue-stv tissue-moderate) (stv 0.68 0.72))
(= (tissue-stv tissue-rare)     (stv 0.45 0.58))

(= (coreg-stv $surp)  (stv $surp 0.80))
(= (coexpr-stv $surp) (stv $surp 0.75))

(= (pathway-stv)   (stv 0.55 0.70))
(= (disease-stv)   (stv 0.70 0.50))
(= (treats-stv)    (stv 0.80 0.90))
(= (palliate-stv)  (stv 0.55 0.72))
(= (binds-stv)     (stv 0.80 0.80))
(= (proximal-stv)  (stv 0.35 0.52))

; ---- Nat / depth -----------------------------------------------------------

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; ---- Typed backward chainer ------------------------------------------------

(: bc (-> $a $a Nat $b $b))
(= (bc $kb $rb $_ (: $prf $thm))
   (match $kb (: $prf $thm) (: $prf $thm)))
(= (bc $kb $rb $_ (: $prf $thm))
   (let* (((leaf-sig $name $raw $prf $thm)
            (match $rb (leaf-sig $name $raw $prf $thm)
                  (leaf-sig $name $raw $prf $thm)))
          ($_hit (match $kb $raw $raw)))
     (: $prf $thm)))
(= (bc $kb $rb $_ (rule-sig $name $type))
   (match $rb (rule-sig $name $type) (rule-sig $name $type)))

(= (bc $kb $rb (S $d) (: ($r $p1) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) $thm))
            (bc $kb $rb $d (rule-sig $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1)
            (bc $kb $rb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))

(= (bc $kb $rb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))
            (bc $kb $rb $d (rule-sig $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1)
            (bc $kb $rb $d (: $p1 $t1)))
          ((: $p2 $t2)
            (bc $kb $rb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

; ---- Proof scoring ---------------------------------------------------------

(= (proof-stv (eqtl-fact $snp $gene))
   (let* (($eff (match &bio-wmpln-kb (effect-class $gene $eff) $eff))
          ($tis (match &bio-wmpln-kb (tissue-class $gene $tis) $tis)))
     (eqtl-stv $eff $tis)))
(= (proof-stv (prox-fact $snp $gene)) (proximal-stv))
(= (proof-stv (coreg-fact $snp1 $snp2 $surp)) (coreg-stv $surp))
(= (proof-stv (coexpr-fact $g1 $g2 $surp)) (coexpr-stv $surp))
(= (proof-stv (tissue-fact $gene $tissue)) (tissue-stv $tissue))
(= (proof-stv (pathway-fact $gene $pw)) (pathway-stv))
(= (proof-stv (disease-fact $gene $disease)) (disease-stv))
(= (proof-stv (treats-fact $comp $disease)) (treats-stv))
(= (proof-stv (palliates-fact $comp $disease)) (palliate-stv))
(= (proof-stv (binds-fact $comp $gene)) (binds-stv))

(= (proof-stv (r1-eqtl-seed $e)) (proof-stv $e))
(= (proof-stv (r2-prox-seed $e)) (proof-stv $e))
(= (proof-stv (r3-contextualize $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r4-coreg-transfer $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r5-coexpr-transfer $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r6-same-pathway $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r7-pathway-transfer $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r8-disease-direct $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r9-disease-coreg $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r10-disease-coexpr $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r11-disease-pathway $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r12-drug-treat $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r13-drug-palliate $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r14-supported-drug $e1 $e2))
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0) (proof-stv $e1) (proof-stv $e2)))

; ---- Proof metadata / revision helpers ------------------------------------

(= (proof-trace-route (r8-disease-direct $e1 $e2)) route-direct)
(= (proof-trace-route (r9-disease-coreg $e1 $e2)) route-coreg)
(= (proof-trace-route (r10-disease-coexpr $e1 $e2)) route-coexpr)
(= (proof-trace-route (r11-disease-pathway $e1 $e2)) route-pathway)
(= (proof-trace-route (r12-drug-treat $e1 $e2)) (proof-trace-route $e1))
(= (proof-trace-route (r13-drug-palliate $e1 $e2)) (proof-trace-route $e1))
(= (proof-trace-route (r14-supported-drug $e1 $e2)) (proof-trace-route $e1))

(= (proof-trace-mode (r12-drug-treat $e1 $e2)) mode-treat)
(= (proof-trace-mode (r13-drug-palliate $e1 $e2)) mode-palliate)
(= (proof-trace-mode (r14-supported-drug $e1 $e2)) (proof-trace-mode $e1))

(= (proof-trace-bound-gene (r14-supported-drug $e1 (binds-fact $drug $gene))) $gene)

(= (revise-stv-step $acc $next)
   (Truth_Revision $acc $next))

(= (revise-stv-tuple ())
   (stv 0.0 0.0))

(= (revise-stv-tuple $tuple)
   (if (== $tuple ())
       (stv 0.0 0.0)
       (let* (($head (car-atom $tuple))
              ($tail (cdr-atom $tuple)))
         (foldl-atom $tail
                     $head
                     $acc
                     $next
                     (eval (revise-stv-step $acc $next))))))

(= (revised-proof-score $tuple)
   (wm-posterior-mean (revise-stv-tuple $tuple)))

; Negative example:
; wrapping `fold` over a proof-result fan-out in an ordinary equation like
; `(revised-proof-results $results)` does not preserve fold semantics, because
; MeTTa evaluates `$results` before the equation fires. Use `fold` directly at
; the call site so the special form sees the whole nondeterministic result set.
