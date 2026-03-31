; ╔══════════════════════════════════════════════════════════════════════╗
; ║  RIPPLE v2 — Proof Discovery + Recursive Scoring                   ║
; ║                                                                    ║
; ║  MM2 discovers proof trees via forward propagation.                ║
; ║  MeTTa scores them via recursive tree traversal.                   ║
; ║  Neither alone can do both.                                        ║
; ╚══════════════════════════════════════════════════════════════════════╝

; ═══════════════════════════════════════════════════════════════════
; KNOWLEDGE BASE — a tiny genomics slice
; ═══════════════════════════════════════════════════════════════════
;
;   SNP rs42 ──eqtl──→ BRCA1 ──disease──→ breast-cancer
;                  └──tissue──→ epithelial     │
;                                    tamoxifen ─┘ (treats)
;   SNP rs42 ──eqtl──→ CDH1  ──disease──→ breast-cancer
;                  └──tissue──→ stromal        │
;                                    tamoxifen ─┘ (treats)
;
; Two independent gene paths to the same disease+drug hypothesis.

!(bind! &ws (new-space mork))

; Raw facts
!(add-atom &ws (eqtl-link rs42 BRCA1))
!(add-atom &ws (eqtl-link rs42 CDH1))
!(add-atom &ws (tissue-class BRCA1 epithelial))
!(add-atom &ws (tissue-class CDH1 stromal))
!(add-atom &ws (disease-gene breast-cancer BRCA1))
!(add-atom &ws (disease-gene breast-cancer CDH1))
!(add-atom &ws (compound-treats tamoxifen breast-cancer))

; ═══════════════════════════════════════════════════════════════════
; MM2 RULES — forward chaining proof construction
; ═══════════════════════════════════════════════════════════════════
; Each rule builds a proof term as it fires.
; The proof term IS the evidence trail — it records HOW
; the conclusion was derived, not just THAT it was derived.

; Priority 1: leaf bridges (raw fact → typed proposition)
!(add-atom &ws (exec (1 r1-eqtl)
  (, (eqtl-link $snp $gene))
  (O (+ (proven (eqtl-fact $snp $gene) (eqtl $snp $gene))))))

!(add-atom &ws (exec (1 r2-tissue)
  (, (tissue-class $gene $tissue))
  (O (+ (proven (tissue-fact $gene $tissue) (has-tissue $gene $tissue))))))

!(add-atom &ws (exec (1 r3-disease)
  (, (disease-gene $disease $gene))
  (O (+ (proven (disease-fact $gene $disease) (has-disease $gene $disease))))))

!(add-atom &ws (exec (1 r4-treats)
  (, (compound-treats $drug $disease))
  (O (+ (proven (treats-fact $drug $disease) (treats $drug $disease))))))

; Priority 2: contextualize (snp+gene+tissue)
!(add-atom &ws (exec (2 r5-context)
  (, (proven $e1 (eqtl $snp $gene))
     (proven $e2 (has-tissue $gene $tissue)))
  (O (+ (proven (r5 $e1 $e2) (contextual-target $snp $gene $tissue))))))

; Priority 3: disease hypothesis (context+disease)
!(add-atom &ws (exec (3 r6-disease-hyp)
  (, (proven $e1 (contextual-target $snp $gene $tissue))
     (proven $e2 (has-disease $gene $disease)))
  (O (+ (proven (r6 $e1 $e2) (disease-hyp $snp $gene $disease $tissue))))))

; Priority 4: drug hypothesis (disease+treatment)
!(add-atom &ws (exec (4 r7-drug-hyp)
  (, (proven $e1 (disease-hyp $snp $gene $disease $tissue))
     (proven $e2 (treats $drug $disease)))
  (O (+ (proven (r7 $e1 $e2) (drug-hyp $snp $gene $disease $drug $tissue))))))

; ═══════════════════════════════════════════════════════════════════
; PHASE 1:  MM2 runs the proof search
; ═══════════════════════════════════════════════════════════════════

!(println! "=== MM2 proof discovery ===")
!(let $n (step! &ws 1000)
   (println! ($n rules fired)))

; What did MM2 find?
!(println! "")
!(println! "--- Drug hypotheses discovered ---")
!(println! (collapse
  (match &ws (proven $proof (drug-hyp $snp $gene $disease $drug $tissue))
    (hypothesis $snp $gene $disease $drug $tissue))))

; Show the proof trees — these are structured terms
!(println! "")
!(println! "--- Proof trees (what MeTTa will score) ---")
!(let (proven $proof (drug-hyp $snp $gene $disease $drug $tissue))
     (match &ws (proven $proof (drug-hyp $snp $gene $disease $drug $tissue))
       (proven $proof (drug-hyp $snp $gene $disease $drug $tissue)))
  (println! (proof-for ($gene $disease $drug) is $proof)))

; ═══════════════════════════════════════════════════════════════════
; PHASE 2:  MeTTa scores the proofs (recursive tree traversal)
; ═══════════════════════════════════════════════════════════════════
; This is the part MM2 CANNOT do — recursive equations over
; structured proof terms. Each node in the proof tree gets a
; truth value, and composition rules combine children's values.

; Leaf truth values — differ by gene to show path comparison
(= (proof-stv (eqtl-fact rs42 BRCA1))        (stv 0.90 0.85))
(= (proof-stv (eqtl-fact rs42 CDH1))         (stv 0.60 0.55))
(= (proof-stv (tissue-fact BRCA1 epithelial)) (stv 0.80 0.75))
(= (proof-stv (tissue-fact CDH1 stromal))     (stv 0.65 0.60))
(= (proof-stv (disease-fact $gene $disease))  (stv 0.70 0.50))
(= (proof-stv (treats-fact $drug $disease))   (stv 0.80 0.90))

; Composition: deduction combines parent and child evidence
; stv = (strength, confidence)
; simplified Truth_Deduction: s = s1*s2, c = c1*c2*0.9
(= (combine-stv (stv $s1 $c1) (stv $s2 $c2))
   (stv (* $s1 $s2) (* (* $c1 $c2) 0.9)))

; Recursive scoring — walks the proof tree
(= (proof-stv (r5 $e1 $e2))
   (combine-stv (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r6 $e1 $e2))
   (combine-stv (proof-stv $e1) (proof-stv $e2)))
(= (proof-stv (r7 $e1 $e2))
   (combine-stv (proof-stv $e1) (proof-stv $e2)))

; Posterior mean with base rate
(= (posterior-mean (stv $s $c))
   (+ (* $s $c) (* (- 1.0 $c) 0.084)))

!(println! "")
!(println! "=== MeTTa proof scoring (recursive — impossible in MM2) ===")

!(let (proven $proof (drug-hyp $snp $gene $disease $drug $tissue))
     (match &ws (proven $proof (drug-hyp $snp $gene $disease $drug $tissue))
       (proven $proof (drug-hyp $snp $gene $disease $drug $tissue)))
  (let $stv (proof-stv $proof)
    (let $score (posterior-mean $stv)
      (println! (scored $gene $disease $drug
                        stv $stv
                        posterior $score)))))

; ═══════════════════════════════════════════════════════════════════
; PHASE 3:  MeTTa compares independent paths (cross-product query)
; ═══════════════════════════════════════════════════════════════════

!(println! "")
!(println! "=== Independent gene paths to same drug target ===")
; Collect all scored drug hypotheses, then show both paths converge
!(let $hyps (collapse
  (match &ws (proven $pa (drug-hyp $sa $ga $da $dra $ta))
    (hyp $ga $ta (posterior-mean (proof-stv $pa)))))
  (println! $hyps))
; Both genes point to the same disease+drug — independently corroborated!
; BRCA1 (epithelial) scores higher than CDH1 (stromal) due to stronger eQTL.

; ═══════════════════════════════════════════════════════════════════
; WHY THIS NEEDS BOTH
; ═══════════════════════════════════════════════════════════════════
;
; MM2 discovers proof trees:
;   (r7 (r6 (r5 (eqtl-fact rs42 BRCA1)
;               (tissue-fact BRCA1 epithelial))
;           (disease-fact BRCA1 breast-cancer))
;       (treats-fact tamoxifen breast-cancer))
;
; MeTTa scores them recursively:
;   proof-stv(r7 e1 e2) = combine(proof-stv(e1), proof-stv(e2))
;   proof-stv(r6 e1 e2) = combine(proof-stv(e1), proof-stv(e2))
;   ... down to leaves
;
; In MM2, this recursive tree fold would need:
;   - Explicit unfold exec rules to decompose each proof node
;   - Wait barriers for each pair of sub-results
;   - Addition/multiplication lookup tables (no native arithmetic)
;   - ~20 extra exec rules of manual stack management
;   - A separate protocol for each rule arity
;
; MeTTa: 7 equations. Done.
