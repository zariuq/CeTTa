; ============================================================================
; PLN Evidence Semantics - Matching Lean Formalization
; ============================================================================
;
; This implements the Evidence type and operations as formalized in:
;   Mettapedia/Logic/PLNEvidence.lean
;
; Key insight from the Lean formalization:
;   Evidence counts (n⁺, n⁻) ∈ ℝ≥0 × ℝ≥0 form a QUANTALE, and
;   SimpleTruthValue (s, c) is a VIEW via the calibration:
;     s = n⁺ / (n⁺ + n⁻)           (strength)
;     c = (n⁺ + n⁻) / (n⁺ + n⁻ + κ) (confidence, with prior κ)
;
; ============================================================================

!(import! &self lib_pln)

; ============================================================================
; EVIDENCE TYPE
; ============================================================================

; Evidence is represented as (evidence pos neg) where:
;   pos = positive evidence count (supports the proposition)
;   neg = negative evidence count (refutes the proposition)
;
; Lean: structure Evidence where pos : ℝ≥0∞; neg : ℝ≥0∞

; Constructor
(= (Evidence $pos $neg) (evidence $pos $neg))

; Accessors
(= (evidence-pos (evidence $pos $neg)) $pos)
(= (evidence-neg (evidence $pos $neg)) $neg)

; Total evidence: n⁺ + n⁻
; Lean: def total (e : Evidence) := e.pos + e.neg
(= (evidence-total (evidence $pos $neg)) (+ $pos $neg))

; ============================================================================
; EVIDENCE OPERATIONS (Quantale Structure)
; ============================================================================

; ----------------------------------------------------------------------------
; Zero Evidence: no support either way
; Lean: def zero := ⟨0, 0⟩
; ----------------------------------------------------------------------------
(= (evidence-zero) (evidence 0 0))

; ----------------------------------------------------------------------------
; Unit Evidence: multiplicative identity for tensor
; Lean: def one := ⟨1, 1⟩
; ----------------------------------------------------------------------------
(= (evidence-one) (evidence 1 1))

; ----------------------------------------------------------------------------
; Parallel Aggregation (hplus): Independent evidence combination
; Lean: def hplus (x y) := ⟨x.pos + y.pos, x.neg + y.neg⟩
;
; This IS the PLN REVISION rule!
; When we have independent sources of evidence, they combine additively.
; ----------------------------------------------------------------------------
(= (evidence-hplus (evidence $p1 $n1) (evidence $p2 $n2))
   (evidence (+ $p1 $p2) (+ $n1 $n2)))

; Alias: revision = hplus
(= (evidence-revision $e1 $e2)
   (evidence-hplus $e1 $e2))

; ----------------------------------------------------------------------------
; Tensor Product: Sequential composition
; Lean: def tensor (x y) := ⟨x.pos * y.pos, x.neg * y.neg⟩
;
; When evidence flows through a chain A → B → C, it compounds multiplicatively.
; ----------------------------------------------------------------------------
(= (evidence-tensor (evidence $p1 $n1) (evidence $p2 $n2))
   (evidence (* $p1 $p2) (* $n1 $n2)))

; ----------------------------------------------------------------------------
; Power Scaling: Regraduation of evidence by informativeness weight
; Lean (to prove): def power (e : Evidence) (w : ℝ≥0) := ⟨e.pos ^ w, e.neg ^ w⟩
;
; Key theorem (log-odds linearity):
;   logOdds(power(e, w)) = w * logOdds(e)
;
; This provides IDF-weighted Naive Bayes:
;   logOdds(prior ⊗ ∏_f power(likelihood_f, w_f))
;     = logOdds(prior) + Σ_f w_f * logOdds(likelihood_f)
;
; Interpretation: w scales how much evidence moves the posterior.
;   w > 1 → amplify rare/informative features
;   w < 1 → attenuate common/uninformative features
;   w = 0 → ignore feature (power returns (1,1) = tensor identity)
;   w = 1 → standard unweighted evidence
; ----------------------------------------------------------------------------
(= (evidence-power (evidence $p $n) $w)
   (evidence (pow-math $p $w) (pow-math $n $w)))

; ============================================================================
; CONVERSION TO SIMPLE TRUTH VALUE
; ============================================================================

; ----------------------------------------------------------------------------
; toStrength: Convert evidence to strength (posterior mean)
; Lean: def toStrength (e) := if e.total = 0 then 0 else e.pos / e.total
;
; Note: Lean returns 0 for undefined case. We follow this exactly.
; ----------------------------------------------------------------------------
(= (evidence-toStrength (evidence $pos $neg))
   (let $total (+ $pos $neg)
        (if (== $total 0)
            0
            (/ $pos $total))))

; ----------------------------------------------------------------------------
; toConfidence: Convert evidence to confidence
; Lean: def toConfidence (e) (κ) := e.total / (e.total + κ)
;
; κ is the prior/context size parameter (default κ = 1)
; Higher total evidence → higher confidence (approaches 1 as evidence → ∞)
; ----------------------------------------------------------------------------
(= (evidence-toConfidence (evidence $pos $neg) $kappa)
   (let $total (+ $pos $neg)
        (/ $total (+ $total $kappa))))

; Default κ = 1
(= (evidence-toConfidence (evidence $pos $neg))
   (evidence-toConfidence (evidence $pos $neg) 1))

; ----------------------------------------------------------------------------
; toSTV: Convert evidence to Simple Truth Value
; Lean: def toSTV (e) := (toStrength(e), toConfidence(e))
; ----------------------------------------------------------------------------
(= (evidence-toSTV (evidence $pos $neg))
   (stv (evidence-toStrength (evidence $pos $neg))
        (evidence-toConfidence (evidence $pos $neg))))

(= (evidence-toSTV (evidence $pos $neg) $kappa)
   (stv (evidence-toStrength (evidence $pos $neg))
        (evidence-toConfidence (evidence $pos $neg) $kappa)))

; ============================================================================
; CONVERSION FROM SIMPLE TRUTH VALUE
; ============================================================================

; ----------------------------------------------------------------------------
; ofSTV: Convert (strength, confidence) back to evidence
; Lean: def ofSTV (s c) (hc : c < 1) :=
;         let total := κ * c / (1 - c)
;         ⟨s * total, (1 - s) * total⟩
;
; Given (s, c) and prior κ, recover (n⁺, n⁻):
;   total = κ * c / (1 - c)
;   n⁺ = s * total
;   n⁻ = (1 - s) * total
; ----------------------------------------------------------------------------
(= (evidence-ofSTV (stv $s $c) $kappa)
   (if (>= $c 1)
       (evidence 0 0)  ; degenerate case
       (let* (($total (/ (* $kappa $c) (- 1 $c)))
              ($pos (* $s $total))
              ($neg (* (- 1 $s) $total)))
             (evidence $pos $neg))))

; Default κ = 1
(= (evidence-ofSTV (stv $s $c))
   (evidence-ofSTV (stv $s $c) 1))

; ============================================================================
; KEY THEOREM: REVISION AS WEIGHTED AVERAGE
; ============================================================================

; The Lean theorem toStrength_hplus proves:
;   toStrength(x + y) = (x.total / (x+y).total) * toStrength(x)
;                     + (y.total / (x+y).total) * toStrength(y)
;
; This is exactly PLN's revision formula: weighted average of strengths!
; We don't prove it here but use the correct formula.

; ============================================================================
; INFERENCE OPERATIONS
; ============================================================================

; ----------------------------------------------------------------------------
; Deduction via Evidence
; Given evidence E_AB (A→B) and E_BC (B→C), compute E_AC (A→C)
;
; The Lean formalization in PLNEvidence.lean:deductionEvidence shows this
; involves both direct path (tensor) and indirect path (via complement).
;
; For simplicity, we delegate to Truth_Deduction which computes the
; strength-level formula. Full evidence-level deduction would need
; marginal probabilities pB, pC.
; ----------------------------------------------------------------------------
(= (evidence-deduction $e_AB $e_BC)
   ; Convert to STV, apply deduction, convert back
   (let* (($stv_AB (evidence-toSTV $e_AB))
          ($stv_BC (evidence-toSTV $e_BC))
          ; Use uniform term probabilities (0.5) for simplified deduction
          ($stv_AC (Truth_Deduction (stv 0.5 1) (stv 0.5 1) (stv 0.5 1)
                                     $stv_AB $stv_BC)))
         (evidence-ofSTV $stv_AC)))

; ============================================================================
; BATCH OPERATIONS FOR PPBENCH
; ============================================================================

; Compute posterior parameters from evidence (matching Lean exactly)
(= (evidence-posterior (evidence $pos $neg))
   (let* (($total (+ $pos $neg))
          ($strength (if (== $total 0) 0 (/ $pos $total)))
          ($confidence (/ $total (+ $total 1))))
         (posterior $strength $confidence $pos $neg)))

; Extract components
(= (posterior-strength (posterior $s $c $p $n)) $s)
(= (posterior-confidence (posterior $s $c $p $n)) $c)
(= (posterior-pos (posterior $s $c $p $n)) $p)
(= (posterior-neg (posterior $s $c $p $n)) $n)

; ============================================================================
; EXAMPLE USAGE
; ============================================================================
;
; ; Create evidence from observations
; !(Evidence 7 3)  ; 7 positive, 3 negative
;
; ; Convert to truth value
; !(evidence-toSTV (Evidence 7 3))
; ; → (stv 0.7 0.909...)  (with κ=1)
;
; ; Combine evidence via revision
; !(evidence-revision (Evidence 4 2) (Evidence 3 1))
; ; → (evidence 7 3)
;
; ; Sequential composition via tensor
; !(evidence-tensor (Evidence 0.9 0.1) (Evidence 0.8 0.2))
; ; → (evidence 0.72 0.02)
;
; ============================================================================
