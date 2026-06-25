!(import! &self ../../lib/lib_wmpln)
!(import! &self ../../lib/lib_overlap_corrected_revision)

; Executable mirror of the crisp Lean bridge in
; Mettapedia/Logic/PLNHigherOrderHOLSimilarityBridge.lean.
;
; A predicate equivalence proof gives two directed predicate-inheritance facts.
; In the collapsed 0/1 case, PLN's 2inh2sim formula turns those two inherited
; strengths into predicate-level similarity.

(= (two-inh-to-sim-crisp $sPQ $sQP)
   (if (or (== $sPQ 0.0) (== $sQP 0.0))
       0.0
       (/ 1.0 (- (+ (/ 1.0 $sPQ) (/ 1.0 $sQP)) 1.0))))

(= (pln-modus-ponens $sAB $sA $c)
   (+ (* $sAB $sA)
      (* $c (- 1.0 $sA))))

(= (pln-symmetric-modus-ponens $simAB $sA $c)
   (+ (* $sA $simAB)
      (* (* $c (- 1.0 $sA)) (+ 1.0 $simAB))))

(= (pln-sim-to-inh $simAB $sA $sB)
   (if (or (== $sA 0.0) (== $simAB -1.0))
       0.0
       (/ (* (+ 1.0 (/ $sB $sA)) $simAB)
          (+ 1.0 $simAB))))

(= (pln-sim-to-inh-hull-lower $simLower $sourceUpper $targetLower)
   (pln-sim-to-inh $simLower $sourceUpper $targetLower))

(= (pln-sim-to-inh-hull-upper $simUpper $sourceLower $targetUpper)
   (pln-sim-to-inh $simUpper $sourceLower $targetUpper))

(= (crisp-inh-strength $holds)
   (if $holds 1.0 0.0))

(= (crisp-predicate-similarity $pImpliesQ $qImpliesP)
   (two-inh-to-sim-crisp
     (crisp-inh-strength $pImpliesQ)
     (crisp-inh-strength $qImpliesP)))

; Saturated predicate-inheritance chain: the Lean theorem proves the generic
; finite-vocabulary version through full extensional/intensional inheritance;
; this executable shadow is just the hand-derived 0/1 endpoint case.
(= (saturated-predicate-inheritance-chain $sPQ $sQR)
   (if (and (== $sPQ 1.0) (== $sQR 1.0))
       1.0
       0.0))

; Saturated analogy transfer: if a similarity endpoint is `1` and the
; neighboring inheritance endpoint is `1`, the transferred inheritance endpoint
; is `1`.  Lean proves the source-side and target-side finite-vocabulary
; theorems; this is the hand-derived 0/1 executable shadow.
(= (saturated-predicate-analogy-transfer $sim $inh)
   (if (and (== $sim 1.0) (== $inh 1.0))
       1.0
       0.0))

; Exact-threshold sim2inh / 2inh2sim cut canaries.  These are intentionally
; only the saturated endpoint cases: below threshold 1, the Lean layer keeps
; soft similarity/inheritance as a semantic envelope rather than claiming
; proof-theoretic tightness.
(= (saturated-similarity-to-directed-inheritance-cut $simEndpoint)
   (if (== $simEndpoint 1.0) 1.0 0.0))

(= (saturated-mutual-inheritance-to-similarity-cut $pqEndpoint $qpEndpoint)
   (if (and (== $pqEndpoint 1.0) (== $qpEndpoint 1.0))
       1.0
       0.0))

(= (pure-extensional-predicate-similarity $pExtendsQ $qExtendsP)
   (two-inh-to-sim-crisp
     (crisp-inh-strength $pExtendsQ)
     (crisp-inh-strength $qExtendsP)))

(= (pure-intensional-predicate-similarity $pIntendsQ $qIntendsP)
   (two-inh-to-sim-crisp
     (crisp-inh-strength $pIntendsQ)
     (crisp-inh-strength $qIntendsP)))

; Executable mirror of the finite-vocabulary order-rank score from
; PLNHigherOrderHOLAssocPatBridge.lean:
; score(left,right) = |Intent(left)| + (|Vocabulary| - |Intent(right)|).
(= (predicate-pair-order-rank-score $leftIntentCard $rightIntentCard $vocabCard)
   (+ $leftIntentCard (- $vocabCard $rightIntentCard)))

; Weighted variant of the finite-vocabulary order-rank score:
; leftWeight * |Intent(left)| + rightWeight * (|Vocabulary|-|Intent(right)|).
; The Lean theorem requires both weights to be nonnegative; the tests below
; include negative-weight counterexamples to make that side condition visible.
(= (weighted-predicate-pair-order-rank-score
      $leftWeight $rightWeight $leftIntentCard $rightIntentCard $vocabCard)
   (+ (* $leftWeight $leftIntentCard)
      (* $rightWeight (- $vocabCard $rightIntentCard))))

; Tiny executable shadow of the mixed extensional+ASSOC+PAT theorem from
; PLNHigherOrderHOLAssocPatBridge.lean.  The Lean theorem is generic in the
; combiner; this canary uses ordinary addition so the boundary is visible:
; same ASSOC/PAT evidence is not enough unless the extensional channel is also
; preserved.
(= (mixed-assoc-pat-sum-evidence $ext $assoc $pat)
   (+ $ext (+ $assoc $pat)))

; Stamped provenance guardrail for the concept-geometry demo.  If two premise
; packets share an extensional or intensional source stamp, guarded revision
; refuses to double-count them.
(= (concept-stamp-overlaps? $leftHasStamp $rightHasStamp)
   (and $leftHasStamp $rightHasStamp))

(= (concept-any-stamp-overlaps?
      $leftHasExtStamp $rightHasExtStamp $leftHasIntStamp $rightHasIntStamp)
   (or (concept-stamp-overlaps? $leftHasExtStamp $rightHasExtStamp)
       (concept-stamp-overlaps? $leftHasIntStamp $rightHasIntStamp)))

(= (guarded-revision-allows? $hasSharedStamp)
   (not $hasSharedStamp))

(= (stamp-guarded-revision-allows? $stampA $stampB)
   (== (StampIntersectionCount $stampA $stampB) 0))

(= (guarded-list-revision-allows3? $xyOverlap $xzOverlap $yzOverlap)
   (not (or $xyOverlap (or $xzOverlap $yzOverlap))))

; Weighted ASSOC/PAT score-family consumer for the same mixed-channel canary.
; This mirrors the Lean semantic-package theorem: weighted finite-vocabulary
; ASSOC and PAT scores may feed the existing mixed channel; the extensional
; evidence remains its own required channel.
(= (weighted-mixed-assoc-pat-sum-evidence
      $ext
      $assocLeftWeight $assocRightWeight
      $patLeftWeight $patRightWeight
      $leftIntentCard $rightIntentCard $vocabCard)
   (mixed-assoc-pat-sum-evidence
     $ext
     (weighted-predicate-pair-order-rank-score
       $assocLeftWeight $assocRightWeight
       $leftIntentCard $rightIntentCard $vocabCard)
     (weighted-predicate-pair-order-rank-score
       $patLeftWeight $patRightWeight
       $leftIntentCard $rightIntentCard $vocabCard)))

; Concept-geometry source fact -> calibrated HO weighted ASSOC/PAT evidence
; monotonicity.  This mirrors the Lean theorem
; positive_pair_subset_rel_weightedPredicateVocabularyAssocPatEvidence_mono_viaIntentCalibration:
; once concrete concept intent is calibrated into the decoded HOL predicate
; vocabulary, the existing nonnegative weighted order-rank score bridge gives
; actual ASSOC and PAT evidence inequalities.
(= (concept-calibrated-weighted-assoc-pat-mono
      $calibrated $sourcePairSubset
      $assocLeftWeight $assocRightWeight
      $patLeftWeight $patRightWeight
      $sourceLeftIntentCard $sourceRightIntentCard
      $targetLeftIntentCard $targetRightIntentCard
      $vocabCard)
   (and
     (and $calibrated $sourcePairSubset)
     (and
       (and (<= 0.0 $assocLeftWeight)
            (<= 0.0 $assocRightWeight))
       (and
         (and (<= 0.0 $patLeftWeight)
              (<= 0.0 $patRightWeight))
         (and
           (<=
             (weighted-predicate-pair-order-rank-score
               $assocLeftWeight $assocRightWeight
               $sourceLeftIntentCard $sourceRightIntentCard $vocabCard)
             (weighted-predicate-pair-order-rank-score
               $assocLeftWeight $assocRightWeight
               $targetLeftIntentCard $targetRightIntentCard $vocabCard))
           (<=
             (weighted-predicate-pair-order-rank-score
               $patLeftWeight $patRightWeight
               $sourceLeftIntentCard $sourceRightIntentCard $vocabCard)
             (weighted-predicate-pair-order-rank-score
               $patLeftWeight $patRightWeight
              $targetLeftIntentCard $targetRightIntentCard $vocabCard)))))))

; Demo-source ASSOC/PAT score channels from AbstractInheritanceIntensionalDemo:
; ASSOC uses intent + extent; PAT deliberately uses intent + 2*extent.  This is
; a structural guardrail: PAT is a distinct channel, not just another name for
; the ASSOC/log-ratio source.
(= (demo-assoc-source-score $intentCard $extentCard)
   (+ $intentCard $extentCard))

(= (demo-pat-source-score $intentCard $extentCard)
   (+ $intentCard (+ $extentCard $extentCard)))

; Threshold-side shadow of the OSLF endpoint: the Lean theorem transports a
; weighted mixed ASSOC/PAT score through the existing interval-threshold atom.
; This executable witness keeps only the hand-computable lower-coordinate case.
(= (weighted-mixed-assoc-pat-threshold-accepts
      $tau $ext
      $assocLeftWeight $assocRightWeight
      $patLeftWeight $patRightWeight
      $leftIntentCard $rightIntentCard $vocabCard)
   (<= $tau
       (weighted-mixed-assoc-pat-sum-evidence
         $ext
         $assocLeftWeight $assocRightWeight
         $patLeftWeight $patRightWeight
         $leftIntentCard $rightIntentCard $vocabCard)))

; Executable shadow of the abstract-pattern-to-HO-intent transport theorem:
; a concrete pattern pair-subset witness feeds the finite-vocabulary HO
; ASSOC/PAT target only when both required intensional transport facts hold.
(= (pattern-pair-subset-transports-to-ho-intent $leftTransport $rightTransport)
   (and $leftTransport $rightTransport))

; Intent-map version of the transport seam.  Reflection maps a decoded HO
; intent item down to a concrete pattern attribute; the concrete inheritance
; relation moves it across the pattern interpretation; preservation maps it
; back into the decoded HO vocabulary.
(= (intent-map-transports-intensional-inheritance
      $reflectsToConcrete $concreteIntensionalInheritance $preservesToHO)
   (and (and $reflectsToConcrete $concreteIntensionalInheritance)
        $preservesToHO))

(= (pattern-pair-subset-transports-to-ho-intent-via-map
      $leftReflect $leftConcrete $leftPreserve
      $rightReflect $rightConcrete $rightPreserve)
   (and
     (intent-map-transports-intensional-inheritance
       $leftReflect $leftConcrete $leftPreserve)
     (intent-map-transports-intensional-inheritance
       $rightReflect $rightConcrete $rightPreserve)))

; Calibrated-intent version of the same transport seam.  The Lean theorem
; packages reflection+preservation as one iff between decoded HO intent
; membership and concrete pattern-side intent membership.
(= (intent-calibration-transports-intensional-inheritance
      $calibrated $concreteIntensionalInheritance)
   (and $calibrated $concreteIntensionalInheritance))

(= (pattern-pair-subset-transports-to-ho-intent-via-calibration
      $leftCalibrated $leftConcrete
      $rightCalibrated $rightConcrete)
   (and
     (intent-calibration-transports-intensional-inheritance
       $leftCalibrated $leftConcrete)
     (intent-calibration-transports-intensional-inheritance
       $rightCalibrated $rightConcrete)))

; Empirical 2x2 Chapter-12 source canary.  In a table with no witness-only
; cases, every witness is also a feature; paired with feature reflexivity, this
; supplies a real empirical pair-subset fact for the calibrated HO bridge.
(= (empirical-witness-feature-pair-subset
      $noWitnessOnly $featureReflexive)
   (and $noWitnessOnly $featureReflexive))

(= (empirical-calibration-transports-pair-subset-to-ho-intent
      $calibrated $empiricalPairSubset)
   (and $calibrated $empiricalPairSubset))

; Finite witness/feature factor-graph table shadow.  The VE witness marginal is
; witnessOnly + both, while the feature&witness joint mass is both.  They are
; equal exactly in this tiny subset canary when witnessOnly = 0.
(= (finite-table-ve-witness-marginal $witnessOnly $both)
   (+ $witnessOnly $both))

(= (finite-table-ve-feature-witness-joint $both)
   $both)

(= (finite-table-ve-witness-eq-feature-witness
      $witnessOnly $both)
   (== (finite-table-ve-witness-marginal $witnessOnly $both)
       (finite-table-ve-feature-witness-joint $both)))

(= (finite-table-ve-feature-marginal $featureOnly $both)
   (+ $featureOnly $both))

(= (finite-table-total $neither $witnessOnly $featureOnly $both)
   (+ $neither (+ $witnessOnly (+ $featureOnly $both))))

; ASSOC/log-ratio source surface for the same finite table:
;   strength = joint / featureSupport
;   prior    = witnessSupport / total
; so the log-ratio argument strength/prior has integer numerator
;   joint * total
; and denominator
;   featureSupport * witnessSupport.
; The executable mirror checks this algebra without needing a floating log.
(= (finite-table-assoc-ratio-numer
      $neither $witnessOnly $featureOnly $both)
   (* (finite-table-ve-feature-witness-joint $both)
      (finite-table-total $neither $witnessOnly $featureOnly $both)))

(= (finite-table-assoc-ratio-denom
      $witnessOnly $featureOnly $both)
   (* (finite-table-ve-feature-marginal $featureOnly $both)
      (finite-table-ve-witness-marginal $witnessOnly $both)))

(= (finite-table-calibration-transports-pair-subset-to-ho-intent
      $calibrated $finiteTablePairSubset)
   (and $calibrated $finiteTablePairSubset))

; Formed-concept Chapter-12 source shadow.  Lean proves this at the actual
; formed-concept interpretation; the executable mirror keeps only the
; hand-computable obligations: a formed-concept pair-subset fact plus a
; calibration between formed intent and decoded HO predicate intent.
(= (formed-concept-calibration-transports-pair-subset-to-ho-intent
      $calibrated $formedConceptPairSubset)
   (and $calibrated $formedConceptPairSubset))

; Actual formed base-concept source canary: the empirical witness attribute is
; closed into a formed concept, the empirical feature attribute is closed into
; a formed concept, and witness inherits feature when there is no witness-only
; mass.  The second input is the reflexive feature->feature half of the
; pair-subset relation.
(= (formed-concept-witness-feature-pair-subset
      $noWitnessOnly $featureReflexive)
   (and $noWitnessOnly $featureReflexive))

; Same-intent / similarity at the formed-concept source is mutual inheritance,
; not merely one directed inclusion.
(= (formed-concept-mutual-inherits
      $witnessInheritsFeature $featureInheritsWitness)
   (and $witnessInheritsFeature $featureInheritsWitness))

(= (formed-concept-same-intent-transports-to-ho
      $calibrated $formedConceptMutualInherits)
   (and $calibrated $formedConceptMutualInherits))

; Rule-facing ASSOC/PAT consumer: once formed-concept same-intent reaches the
; HO predicate vocabulary, the intensional ASSOC and PAT channels may be
; replaced by equal score-derived evidence.  This mirror keeps only the
; hand-computable equality checks.
(= (formed-concept-same-intent-assoc-pat-evidence-eq
      $calibrated $formedConceptMutualInherits
      $assocLeft $assocRight $patLeft $patRight)
   (and
     (formed-concept-same-intent-transports-to-ho
       $calibrated $formedConceptMutualInherits)
     (and (== $assocLeft $assocRight)
          (== $patLeft $patRight))))

; Mixed evidence has one extra honest side condition: extensional evidence must
; also match. Same-intent is an intensional replacement theorem, not a license
; to infer extensional equality.
(= (formed-concept-same-intent-mixed-evidence-eq
      $extensionalEq $assocPatEq)
   (and $extensionalEq $assocPatEq))

; Formed-concept inheritance tables are ordinary finite witness/feature tables
; at the VE layer.  If there are no witness-only cases, witness marginal equals
; feature&witness joint; otherwise the subset-style source fact is broken.
(= (formed-concept-table-ve-witness-eq-feature-witness
      $witnessOnly $both)
   (finite-table-ve-witness-eq-feature-witness $witnessOnly $both))

(= (formed-concept-table-assoc-ratio-numer
      $neither $witnessOnly $featureOnly $both)
   (finite-table-assoc-ratio-numer
      $neither $witnessOnly $featureOnly $both))

(= (formed-concept-table-assoc-ratio-denom
      $witnessOnly $featureOnly $both)
   (finite-table-assoc-ratio-denom
      $witnessOnly $featureOnly $both))

; Generic finite feature/witness count-table criterion: if all support for a
; witness state is concentrated in one feature state, the VE witness marginal
; equals the feature&witness joint query for that state.
(= (generic-count-table-ve-witness-eq-feature-witness
      $witnessSupport $featureWitnessJoint)
   (== $witnessSupport $featureWitnessJoint))

; Executable mirror of PLNHigherOrderHOLQuantifierBridge.lean's finite
; crisp-profile/QFM seam over a two-object carrier. This is the runnable
; shadow of the Lean satisfying-set reduction, not a proof of it.
(= (predicate-crisp-profile $holds)
   (if $holds 1.0 0.0))

(= (predicate-crisp-near-zero-profile $holds)
   (if $holds 0.0 1.0))

(= (finite-two-near-one-fraction $leftHolds $rightHolds)
   (/ (+ (predicate-crisp-profile $leftHolds)
         (predicate-crisp-profile $rightHolds))
      2.0))

(= (finite-two-near-zero-fraction $leftHolds $rightHolds)
   (/ (+ (predicate-crisp-near-zero-profile $leftHolds)
         (predicate-crisp-near-zero-profile $rightHolds))
      2.0))

(= (finite-two-qfm-forall $pcl $leftHolds $rightHolds)
   (<= $pcl (finite-two-near-one-fraction $leftHolds $rightHolds)))

(= (finite-two-qfm-there-exists $pcl $leftHolds $rightHolds)
   (<= $pcl (- 1.0 (finite-two-near-zero-fraction $leftHolds $rightHolds))))

(= (finite-two-qfm-interval-holds $lpc $upc $leftHolds $rightHolds)
   (and (<= $lpc (finite-two-near-one-fraction $leftHolds $rightHolds))
        (<= (finite-two-near-one-fraction $leftHolds $rightHolds) $upc)))

(= (finite-two-logical-exists $leftHolds $rightHolds)
   (or $leftHolds $rightHolds))

(= (finite-two-logical-forall $leftHolds $rightHolds)
   (and $leftHolds $rightHolds))

; Executable mirror of the arbitrary-domain/counting reduction: on a finite
; two-object carrier with normalized counting capacity, the "inf" proxy-cut
; quantities reduce to the finite QFM quantities above.
(= (finite-two-inf-counting-near-one-mass $leftHolds $rightHolds)
   (finite-two-near-one-fraction $leftHolds $rightHolds))

(= (finite-two-inf-counting-near-zero-mass $leftHolds $rightHolds)
   (finite-two-near-zero-fraction $leftHolds $rightHolds))

(= (finite-two-inf-counting-qfm-forall $pcl $leftHolds $rightHolds)
   (finite-two-qfm-forall $pcl $leftHolds $rightHolds))

(= (finite-two-inf-counting-qfm-there-exists $pcl $leftHolds $rightHolds)
   (finite-two-qfm-there-exists $pcl $leftHolds $rightHolds))

(= (finite-two-inf-counting-qfm-interval-holds $lpc $upc $leftHolds $rightHolds)
   (finite-two-qfm-interval-holds $lpc $upc $leftHolds $rightHolds))

; Concrete non-counting capacity shadow of the arbitrary-capacity HOL theorem.
; This is one weighted two-object capacity example, not the generic Lean proof.
(= (finite-two-weighted-near-one-mass
      $leftWeight $rightWeight $leftHolds $rightHolds)
   (+ (if $leftHolds $leftWeight 0.0)
      (if $rightHolds $rightWeight 0.0)))

(= (finite-two-weighted-near-zero-mass
      $leftWeight $rightWeight $leftHolds $rightHolds)
   (+ (if $leftHolds 0.0 $leftWeight)
      (if $rightHolds 0.0 $rightWeight)))

; Explicit executable shadow of the Lean extension/complement-capacity split:
; near-one mass is capacity(extension(P)); near-zero mass is
; capacity(complement(extension(P))).
(= (finite-two-weighted-extension-capacity
      $leftWeight $rightWeight $leftHolds $rightHolds)
   (finite-two-weighted-near-one-mass
     $leftWeight $rightWeight $leftHolds $rightHolds))

(= (finite-two-weighted-complement-extension-capacity
      $leftWeight $rightWeight $leftHolds $rightHolds)
   (finite-two-weighted-near-zero-mass
     $leftWeight $rightWeight $leftHolds $rightHolds))

(= (finite-two-weighted-qfm-forall
      $pcl $leftWeight $rightWeight $leftHolds $rightHolds)
   (<= $pcl
       (finite-two-weighted-near-one-mass
         $leftWeight $rightWeight $leftHolds $rightHolds)))

(= (finite-two-weighted-qfm-there-exists
      $pcl $leftWeight $rightWeight $leftHolds $rightHolds)
   (<= $pcl
       (- 1.0
          (finite-two-weighted-near-zero-mass
            $leftWeight $rightWeight $leftHolds $rightHolds))))

(= (finite-two-weighted-qfm-there-exists-via-complement-capacity
      $pcl $leftWeight $rightWeight $leftHolds $rightHolds)
   (<= $pcl
       (- 1.0
          (finite-two-weighted-complement-extension-capacity
            $leftWeight $rightWeight $leftHolds $rightHolds))))

(= (finite-two-weighted-qfm-two-inh2sim-forall
      $leftWeight $rightWeight
      $forwardLeftHolds $forwardRightHolds
      $reverseLeftHolds $reverseRightHolds)
   (two-inh-to-sim-crisp
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $forwardLeftHolds $forwardRightHolds)
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $reverseLeftHolds $reverseRightHolds)))

(= (finite-two-weighted-qfm-two-inh2sim-there-exists
      $leftWeight $rightWeight
      $forwardLeftHolds $forwardRightHolds
      $reverseLeftHolds $reverseRightHolds)
   (two-inh-to-sim-crisp
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $forwardLeftHolds $forwardRightHolds))
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $reverseLeftHolds $reverseRightHolds))))

(= (finite-two-weighted-qfm-modus-ponens-forall
      $background $leftWeight $rightWeight
      $impLeftHolds $impRightHolds
      $premiseLeftHolds $premiseRightHolds)
   (pln-modus-ponens
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $impLeftHolds $impRightHolds)
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $premiseLeftHolds $premiseRightHolds)
     $background))

(= (finite-two-weighted-qfm-modus-ponens-there-exists
      $background $leftWeight $rightWeight
      $impLeftHolds $impRightHolds
      $premiseLeftHolds $premiseRightHolds)
   (pln-modus-ponens
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $impLeftHolds $impRightHolds))
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $premiseLeftHolds $premiseRightHolds))
     $background))

(= (finite-two-weighted-qfm-symmetric-modus-ponens-forall
      $background $leftWeight $rightWeight
      $simLeftHolds $simRightHolds
      $premiseLeftHolds $premiseRightHolds)
   (pln-symmetric-modus-ponens
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $simLeftHolds $simRightHolds)
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $premiseLeftHolds $premiseRightHolds)
     $background))

(= (finite-two-weighted-qfm-symmetric-modus-ponens-there-exists
      $background $leftWeight $rightWeight
      $simLeftHolds $simRightHolds
      $premiseLeftHolds $premiseRightHolds)
   (pln-symmetric-modus-ponens
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $simLeftHolds $simRightHolds))
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $premiseLeftHolds $premiseRightHolds))
     $background))

(= (finite-two-weighted-qfm-sim-to-inh-forall
      $leftWeight $rightWeight
      $simLeftHolds $simRightHolds
      $sourceLeftHolds $sourceRightHolds
      $targetLeftHolds $targetRightHolds)
   (pln-sim-to-inh
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $simLeftHolds $simRightHolds)
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $sourceLeftHolds $sourceRightHolds)
     (finite-two-weighted-near-one-mass
       $leftWeight $rightWeight $targetLeftHolds $targetRightHolds)))

(= (finite-two-weighted-qfm-sim-to-inh-there-exists
      $leftWeight $rightWeight
      $simLeftHolds $simRightHolds
      $sourceLeftHolds $sourceRightHolds
      $targetLeftHolds $targetRightHolds)
   (pln-sim-to-inh
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $simLeftHolds $simRightHolds))
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $sourceLeftHolds $sourceRightHolds))
     (- 1.0
        (finite-two-weighted-near-zero-mass
          $leftWeight $rightWeight $targetLeftHolds $targetRightHolds))))

(= (finite-two-weighted-qfm-interval-holds
      $lpc $upc $leftWeight $rightWeight $leftHolds $rightHolds)
   (and (<= $lpc
            (finite-two-weighted-near-one-mass
              $leftWeight $rightWeight $leftHolds $rightHolds))
        (<= (finite-two-weighted-near-one-mass
              $leftWeight $rightWeight $leftHolds $rightHolds)
            $upc)))

(= (one-point-event-probability $eventHolds)
   (if $eventHolds 1.0 0.0))

(= (predicate-equivalence-event $pImpliesQ $qImpliesP)
   (and $pImpliesQ $qImpliesP))

(= (one-point-hierarchical-predicate-similarity $pImpliesQ $qImpliesP)
   (one-point-event-probability
     (predicate-equivalence-event $pImpliesQ $qImpliesP)))

(= (one-point-hierarchical-two-inh2sim-similarity $pImpliesQ $qImpliesP)
   (two-inh-to-sim-crisp
     (one-point-event-probability $pImpliesQ)
     (one-point-event-probability $qImpliesP)))

(= (credal-interval-lower-singleton $value)
   $value)

(= (credal-interval-upper-singleton $value)
   $value)

(= (one-point-credal-predicate-similarity-lower $pImpliesQ $qImpliesP)
   (credal-interval-lower-singleton
     (one-point-hierarchical-predicate-similarity $pImpliesQ $qImpliesP)))

(= (one-point-credal-predicate-similarity-upper $pImpliesQ $qImpliesP)
   (credal-interval-upper-singleton
     (one-point-hierarchical-predicate-similarity $pImpliesQ $qImpliesP)))

(= (credal-interval-lower-two-bool $left $right)
   (if (or (== $left 0.0) (== $right 0.0))
       0.0
       1.0))

(= (credal-interval-upper-two-bool $left $right)
   (if (or (== $left 1.0) (== $right 1.0))
       1.0
       0.0))

(= (two-completion-credal-predicate-similarity-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-lower-two-bool
     (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
     (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)))

(= (two-completion-credal-predicate-similarity-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-upper-two-bool
     (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
     (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)))

(= (itv-lower $lower $upper $credibility)
   $lower)

(= (itv-upper $lower $upper $credibility)
   $upper)

(= (itv-credibility $lower $upper $credibility)
   $credibility)

(= (itv-width $lower $upper $credibility)
   (- $upper $lower))

; Executable shadow of the HO completeness-tightness decidedness theorem.
; Lean proves the generic endpoint result; this MeTTa canary only checks the
; hand-derived interval arithmetic: decided [1,1] has width 0, open [0,1]
; has width 1.
(= (canonical-decided-query-width)
   (itv-width 1.0 1.0 1.0))

(= (canonical-open-query-width)
   (itv-width 0.0 1.0 0.0))

(= (finite-two-product-multi-join $x $y)
   (* $x $y))

(= (finite-two-noisy-or-multi-join $x $y)
   (- 1.0 (* (- 1.0 $x) (- 1.0 $y))))

(= (finite-two-complement-product-multi-join $x $y)
   (finite-two-product-multi-join (- 1.0 $x) (- 1.0 $y)))

(= (finite-two-one-sub-product-multi-join $x $y)
   (- 1.0 (finite-two-product-multi-join $x $y)))

(= (finite-two-product-complement-plus-noisy-or $x $y)
   (+ (finite-two-complement-product-multi-join $x $y)
      (finite-two-noisy-or-multi-join $x $y)))

(= (finite-two-noisy-or-complement-dual $x $y)
   (finite-two-noisy-or-multi-join (- 1.0 $x) (- 1.0 $y)))

(= (one-point-product-multi-join-itv-width $x $y $credibility)
   (itv-width
     (credal-interval-lower-singleton
       (finite-two-product-multi-join $x $y))
     (credal-interval-upper-singleton
       (finite-two-product-multi-join $x $y))
     $credibility))

(= (one-point-noisy-or-multi-join-itv-width $x $y $credibility)
   (itv-width
     (credal-interval-lower-singleton
       (finite-two-noisy-or-multi-join $x $y))
     (credal-interval-upper-singleton
       (finite-two-noisy-or-multi-join $x $y))
     $credibility))

(= (one-point-credal-predicate-similarity-itv-width
      $pImpliesQ $qImpliesP $credibility)
   (itv-width
     (one-point-credal-predicate-similarity-lower $pImpliesQ $qImpliesP)
     (one-point-credal-predicate-similarity-upper $pImpliesQ $qImpliesP)
     $credibility))

(= (two-completion-credal-predicate-similarity-itv-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-lower
     (two-completion-credal-predicate-similarity-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-credal-predicate-similarity-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-credal-predicate-similarity-itv-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-upper
     (two-completion-credal-predicate-similarity-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-credal-predicate-similarity-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-credal-predicate-similarity-itv-credibility
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-credibility
     (two-completion-credal-predicate-similarity-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-credal-predicate-similarity-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-credal-predicate-similarity-itv-width
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-width
     (two-completion-credal-predicate-similarity-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-credal-predicate-similarity-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-two-inh2sim-predicate-similarity-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-lower-two-bool
     (one-point-hierarchical-two-inh2sim-similarity $pImpliesQ1 $qImpliesP1)
     (one-point-hierarchical-two-inh2sim-similarity $pImpliesQ2 $qImpliesP2)))

(= (two-completion-two-inh2sim-predicate-similarity-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-upper-two-bool
     (one-point-hierarchical-two-inh2sim-similarity $pImpliesQ1 $qImpliesP1)
     (one-point-hierarchical-two-inh2sim-similarity $pImpliesQ2 $qImpliesP2)))

(= (two-completion-two-inh2sim-predicate-similarity-itv-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-lower
     (two-completion-two-inh2sim-predicate-similarity-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-two-inh2sim-predicate-similarity-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-two-inh2sim-predicate-similarity-itv-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-upper
     (two-completion-two-inh2sim-predicate-similarity-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-two-inh2sim-predicate-similarity-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-two-inh2sim-predicate-similarity-itv-credibility
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-credibility
     (two-completion-two-inh2sim-predicate-similarity-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-two-inh2sim-predicate-similarity-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (binary-rule-product $left $right)
   (* $left $right))

(= (two-completion-product-similarity-rule-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-lower-two-bool
     (binary-rule-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1))
     (binary-rule-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2))))

(= (two-completion-product-similarity-rule-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-upper-two-bool
     (binary-rule-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1))
     (binary-rule-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2))))

(= (two-completion-product-similarity-rule-itv-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-lower
     (two-completion-product-similarity-rule-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-product-similarity-rule-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-product-similarity-rule-itv-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-upper
     (two-completion-product-similarity-rule-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-product-similarity-rule-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-product-similarity-rule-itv-credibility
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-credibility
     (two-completion-product-similarity-rule-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-product-similarity-rule-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (finite-rule-tri-product $x $y $z)
   (* (* $x $y) $z))

(= (two-completion-tri-product-similarity-rule-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-lower-two-bool
     (finite-rule-tri-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1))
     (finite-rule-tri-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2))))

(= (two-completion-tri-product-similarity-rule-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
   (credal-interval-upper-two-bool
     (finite-rule-tri-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1)
       (one-point-hierarchical-predicate-similarity $pImpliesQ1 $qImpliesP1))
     (finite-rule-tri-product
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2)
       (one-point-hierarchical-predicate-similarity $pImpliesQ2 $qImpliesP2))))

(= (two-completion-tri-product-similarity-rule-itv-lower
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-lower
     (two-completion-tri-product-similarity-rule-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-tri-product-similarity-rule-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-tri-product-similarity-rule-itv-upper
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-upper
     (two-completion-tri-product-similarity-rule-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-tri-product-similarity-rule-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

(= (two-completion-tri-product-similarity-rule-itv-credibility
      $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2 $credibility)
   (itv-credibility
     (two-completion-tri-product-similarity-rule-lower
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     (two-completion-tri-product-similarity-rule-upper
       $pImpliesQ1 $qImpliesP1 $pImpliesQ2 $qImpliesP2)
     $credibility))

!(assertEqual (crisp-predicate-similarity True True) 1.0)
!(assertEqual (crisp-predicate-similarity True False) 0.0)
!(assertEqual (crisp-predicate-similarity False True) 0.0)
!(assertEqual (crisp-predicate-similarity False False) 0.0)
!(assertEqual
  (== (crisp-predicate-similarity True False)
      (crisp-predicate-similarity False True))
  True)

!(assertEqual
  (saturated-predicate-inheritance-chain
    (crisp-inh-strength True)
    (crisp-inh-strength True))
  1.0)
!(assertEqual
  (saturated-predicate-inheritance-chain
    (crisp-inh-strength True)
    (crisp-inh-strength False))
  0.0)
!(assertEqual
  (saturated-predicate-inheritance-chain
    (crisp-inh-strength False)
    (crisp-inh-strength True))
  0.0)
!(assertEqual
  (== (saturated-predicate-inheritance-chain
        (crisp-inh-strength True)
        (crisp-inh-strength False))
      1.0)
  False)

!(assertEqual
  (saturated-predicate-analogy-transfer
    (crisp-predicate-similarity True True)
    (crisp-inh-strength True))
  1.0)
!(assertEqual
  (saturated-predicate-analogy-transfer
    (crisp-predicate-similarity True False)
    (crisp-inh-strength True))
  0.0)
!(assertEqual
  (saturated-predicate-analogy-transfer
    (crisp-predicate-similarity True True)
    (crisp-inh-strength False))
  0.0)
!(assertEqual
  (== (saturated-predicate-analogy-transfer
        (crisp-predicate-similarity True False)
        (crisp-inh-strength True))
      1.0)
  False)

!(assertEqual
  (saturated-similarity-to-directed-inheritance-cut
    (crisp-predicate-similarity True True))
  1.0)
!(assertEqual
  (saturated-similarity-to-directed-inheritance-cut
    (crisp-predicate-similarity True False))
  0.0)
!(assertEqual
  (saturated-mutual-inheritance-to-similarity-cut
    (crisp-inh-strength True)
    (crisp-inh-strength True))
  1.0)
!(assertEqual
  (saturated-mutual-inheritance-to-similarity-cut
    (crisp-inh-strength True)
    (crisp-inh-strength False))
  0.0)
!(assertEqual
  (== (saturated-mutual-inheritance-to-similarity-cut
        (crisp-inh-strength True)
        (crisp-inh-strength False))
      1.0)
  False)

!(assertEqual (pure-extensional-predicate-similarity True True) 1.0)
!(assertEqual (pure-extensional-predicate-similarity True False) 0.0)
!(assertEqual (pure-intensional-predicate-similarity True True) 1.0)
!(assertEqual (pure-intensional-predicate-similarity True False) 0.0)
!(assertEqual
  (== (pure-intensional-predicate-similarity True False)
      (pure-intensional-predicate-similarity False True))
  True)

!(assertEqual (predicate-pair-order-rank-score 1.0 2.0 3.0) 2.0)
!(assertEqual (predicate-pair-order-rank-score 2.0 1.0 3.0) 4.0)
!(assertEqual
  (<= (predicate-pair-order-rank-score 1.0 2.0 3.0)
      (predicate-pair-order-rank-score 2.0 1.0 3.0))
  True)
!(assertEqual
  (== (predicate-pair-order-rank-score 2.0 1.0 3.0)
      (predicate-pair-order-rank-score 2.0 1.0 3.0))
  True)
!(assertEqual
  (weighted-predicate-pair-order-rank-score 1.0 1.0 2.0 1.0 3.0)
  (predicate-pair-order-rank-score 2.0 1.0 3.0))
!(assertEqual
  (weighted-predicate-pair-order-rank-score 2.0 3.0 2.0 1.0 4.0)
  13.0)
; Positive weighted ASSOC/PAT shadow: with nonnegative weights, strengthening
; the left intent and weakening/specializing the right intent increases score.
!(assertEqual
  (<= (weighted-predicate-pair-order-rank-score 2.0 3.0 1.0 3.0 4.0)
      (weighted-predicate-pair-order-rank-score 2.0 3.0 2.0 1.0 4.0))
  True)
; If weights are at least 1, the weighted score pointwise dominates the
; unweighted order-rank witness.
!(assertEqual
  (<= (predicate-pair-order-rank-score 2.0 1.0 4.0)
      (weighted-predicate-pair-order-rank-score 2.0 3.0 2.0 1.0 4.0))
  True)
; Nonnegative weights below 1 are still monotone, but they need not dominate
; the original order-rank witness.
!(assertEqual
  (<= (predicate-pair-order-rank-score 2.0 1.0 4.0)
      (weighted-predicate-pair-order-rank-score 0.5 0.5 2.0 1.0 4.0))
  False)
; Negative left-weight canary: monotonicity can fail if a score designer uses a
; negative weight on the left intent-size channel.
!(assertEqual
  (<= (weighted-predicate-pair-order-rank-score -1.0 0.0 1.0 3.0 4.0)
      (weighted-predicate-pair-order-rank-score -1.0 0.0 2.0 3.0 4.0))
  False)
; Negative right-weight canary: a negative deficit weight reverses the
; right-hand concept-geometry channel.
!(assertEqual
  (<= (weighted-predicate-pair-order-rank-score 0.0 -1.0 1.0 3.0 4.0)
      (weighted-predicate-pair-order-rank-score 0.0 -1.0 1.0 1.0 4.0))
  False)
; Concrete concept-geometry consumer: bird/bird <= penguin/bird after
; calibrated HOL intent transport.  Hand arithmetic:
; ASSOC source = 2*1 + 3*(4-1) = 11, target = 2*2 + 3*(4-1) = 13;
; PAT source = 1*1 + 2*(4-1) = 7, target = 1*2 + 2*(4-1) = 8.
!(assertEqual
  (concept-calibrated-weighted-assoc-pat-mono
    True True
    2.0 3.0
    1.0 2.0
    1.0 1.0
    2.0 1.0
    4.0)
  True)
; Calibration into the HOL predicate vocabulary is load-bearing.
!(assertEqual
  (concept-calibrated-weighted-assoc-pat-mono
    False True
    2.0 3.0
    1.0 2.0
    1.0 1.0
    2.0 1.0
    4.0)
  False)
; The concrete source pair-subset witness is also load-bearing.
!(assertEqual
  (concept-calibrated-weighted-assoc-pat-mono
    True False
    2.0 3.0
    1.0 2.0
    1.0 1.0
    2.0 1.0
    4.0)
  False)
; Negative weights are rejected because they can reverse monotonicity.
!(assertEqual
  (concept-calibrated-weighted-assoc-pat-mono
    True True
    -1.0 0.0
    1.0 2.0
    1.0 1.0
    2.0 1.0
    4.0)
  False)
; PAT/ASSOC noncollapse canary for the live toy ontology.  Bird has one
; self-intent item and two positive extent witnesses, so ASSOC = 1+2 = 3
; while PAT = 1+2+2 = 5.
!(assertEqual (demo-assoc-source-score 1.0 2.0) 3.0)
!(assertEqual (demo-pat-source-score 1.0 2.0) 5.0)
!(assertEqual
  (== (demo-assoc-source-score 1.0 2.0)
      (demo-pat-source-score 1.0 2.0))
  False)
; Negative/boundary guardrail: if the consequent extent channel is absent,
; this particular PAT-vs-ASSOC separation disappears.  Thus the theorem is a
; nonempty-source separation, not a global claim that the channels always differ.
!(assertEqual
  (== (demo-assoc-source-score 4.0 0.0)
      (demo-pat-source-score 4.0 0.0))
  True)
; Extensional provenance-overlap canary: `pingu` is a positive extent stamp for
; both penguin->bird and bird->bird, so guarded revision refuses the premise
; merge.  If one side lacks the stamp, this particular guard does not fire.
!(assertEqual (concept-stamp-overlaps? True True) True)
!(assertEqual
  (guarded-revision-allows? (concept-stamp-overlaps? True True))
  False)
!(assertEqual (concept-stamp-overlaps? True False) False)
!(assertEqual
  (guarded-revision-allows? (concept-stamp-overlaps? True False))
  True)
; Dual-channel provenance guardrail: either a shared extensional source or a
; shared intensional source is enough to reject revision; with neither source
; shared, this guard does not fire.
!(assertEqual (concept-any-stamp-overlaps? True True False False) True)
!(assertEqual (concept-any-stamp-overlaps? False False True True) True)
!(assertEqual (concept-any-stamp-overlaps? True False True False) False)
!(assertEqual
  (guarded-revision-allows?
    (concept-any-stamp-overlaps? False False False False))
  True)
!(assertEqual
  (guarded-revision-allows?
    (concept-any-stamp-overlaps? False True True True))
  False)
; Multi-packet Revision guard: a list aggregate is allowed exactly when no
; pair of packets overlaps.  This mirrors the Lean list-level iff, not a new
; scoring semantics.
!(assertEqual (guarded-list-revision-allows3? False False False) True)
!(assertEqual (guarded-list-revision-allows3? True False False) False)
!(assertEqual (guarded-list-revision-allows3? False True False) False)
!(assertEqual (guarded-list-revision-allows3? False False True) False)
; Internal finite-inheritance Revision canary: positive and negative witness
; packets use distinct provenance constructors in Lean, so guarded Revision
; allows them and overlap-corrected Revision agrees with raw evidence Revision.
; Hand: stv(1,1/2) is one positive unit; stv(0,1/2) is one negative unit;
; combined evidence has strength 1/2 and confidence 2/3.
!(assertEqual (stamp-guarded-revision-allows? (101) (201)) True)
!(assertEqual
  (Truth_OverlapCorrectedRevision (stv 1.0 0.5) (stv 0.0 0.5) (101) (201))
  (Truth_Revision (stv 1.0 0.5) (stv 0.0 0.5)))
!(assertEqual
  (Truth_OverlapCorrectedRevision (stv 1.0 0.5) (stv 0.0 0.5) (101) (201))
  (stv 0.5 0.6666666666666666))
; Boundary: if two packets reuse the same provenance stamp, the guard rejects
; the merge and corrected Revision no longer equals raw additive Revision.
!(assertEqual (stamp-guarded-revision-allows? (101) (101)) False)
!(assertEqual
  (== (Truth_OverlapCorrectedRevision (stv 1.0 0.5) (stv 0.0 0.5) (101) (101))
      (Truth_Revision (stv 1.0 0.5) (stv 0.0 0.5)))
  False)
!(assertEqual (mixed-assoc-pat-sum-evidence 0.4 0.7 0.9) 2.0)
; Weighted mixed-channel positive canary:
; assoc = 2*2 + 3*(4-1) = 13; pat = 1*2 + 2*(4-1) = 8; ext = 4.
!(assertEqual
  (weighted-mixed-assoc-pat-sum-evidence
    4.0 2.0 3.0 1.0 2.0 2.0 1.0 4.0)
  25.0)
!(assertEqual
  (weighted-mixed-assoc-pat-threshold-accepts
    20.0 4.0 2.0 3.0 1.0 2.0 2.0 1.0 4.0)
  True)
!(assertEqual
  (weighted-mixed-assoc-pat-threshold-accepts
    26.0 4.0 2.0 3.0 1.0 2.0 2.0 1.0 4.0)
  False)
; Pattern-semantics transport canary: the positive case mirrors the concrete
; bird/bird <= penguin/bird pair-subset transport theorem; the negative case
; shows that losing either transported intensional half blocks the HO target.
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent True True)
  True)
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent False True)
  False)
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent-via-map
    True True True True True True)
  True)
; Reflection is load-bearing: without a concrete attribute image for a decoded
; HO intent item, the transport theorem cannot fire.
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent-via-map
    False True True True True True)
  False)
; Concrete pattern-side inheritance is also load-bearing: map laws alone do not
; prove the HO finite-vocabulary pair relation.
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent-via-map
    True False True True True True)
  False)
; Preservation back into the HO vocabulary is load-bearing too.
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent-via-map
    True True False True True True)
  False)
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent-via-calibration
    True True True True)
  True)
; Losing the calibration iff blocks transport even when the concrete
; inheritance fact itself is present.
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent-via-calibration
    False True True True)
  False)
; Calibration alone does not manufacture concrete intensional inheritance.
!(assertEqual
  (pattern-pair-subset-transports-to-ho-intent-via-calibration
    True False True True)
  False)
!(assertEqual
  (empirical-witness-feature-pair-subset True True)
  True)
; If witness-only cases exist, the tiny empirical witness->feature canary no
; longer supplies the pair-subset fact.
!(assertEqual
  (empirical-witness-feature-pair-subset False True)
  False)
!(assertEqual
  (empirical-calibration-transports-pair-subset-to-ho-intent
    True
    (empirical-witness-feature-pair-subset True True))
  True)
; The empirical pair-subset fact is not enough without a calibration between
; empirical intent and decoded HO predicate-vocabulary intent.
!(assertEqual
  (empirical-calibration-transports-pair-subset-to-ho-intent
    False
    (empirical-witness-feature-pair-subset True True))
  False)
!(assertEqual
  (finite-table-ve-witness-marginal 0 1)
  1)
!(assertEqual
  (finite-table-ve-feature-witness-joint 1)
  1)
!(assertEqual
  (finite-table-ve-witness-eq-feature-witness 0 1)
  True)
; A witness-only case breaks the table-level subset fact: the witness marginal
; is now larger than the feature&witness joint mass.
!(assertEqual
  (finite-table-ve-witness-eq-feature-witness 1 1)
  False)
; The same source table also carries the ASSOC log-ratio readout.  For the
; witnessImpliesFeature table (neither=1, witnessOnly=0, featureOnly=1, both=1):
;   strength = 1 / 2, prior = 1 / 3, strength/prior = 3 / 2.
!(assertEqual
  (finite-table-ve-feature-marginal 1 1)
  2)
!(assertEqual
  (finite-table-total 1 0 1 1)
  3)
!(assertEqual
  (finite-table-assoc-ratio-numer 1 0 1 1)
  3)
!(assertEqual
  (finite-table-assoc-ratio-denom 0 1 1)
  2)
; Negative guardrail: a log-ratio source can be perfectly well-defined even
; when the pair-subset source fact fails because witness-only mass is present.
!(assertEqual
  (finite-table-ve-witness-eq-feature-witness 1 1)
  False)
!(assertEqual
  (finite-table-assoc-ratio-numer 1 1 0 1)
  3)
!(assertEqual
  (finite-table-assoc-ratio-denom 1 0 1)
  2)
!(assertEqual
  (finite-table-calibration-transports-pair-subset-to-ho-intent
    True
    (finite-table-ve-witness-eq-feature-witness 0 1))
  True)
!(assertEqual
  (finite-table-calibration-transports-pair-subset-to-ho-intent
    False
    (finite-table-ve-witness-eq-feature-witness 0 1))
  False)
!(assertEqual
  (formed-concept-witness-feature-pair-subset True True)
  True)
!(assertEqual
  (formed-concept-witness-feature-pair-subset False True)
  False)
!(assertEqual
  (formed-concept-calibration-transports-pair-subset-to-ho-intent
    True
    (formed-concept-witness-feature-pair-subset True True))
  True)
!(assertEqual
  (formed-concept-calibration-transports-pair-subset-to-ho-intent
    False
    (formed-concept-witness-feature-pair-subset True True))
  False)
!(assertEqual
  (formed-concept-mutual-inherits True True)
  True)
; One-way inheritance is not formed-concept same-intent.
!(assertEqual
  (formed-concept-mutual-inherits True False)
  False)
!(assertEqual
  (formed-concept-same-intent-transports-to-ho
    True
    (formed-concept-mutual-inherits True True))
  True)
; Calibration is still load-bearing for same-intent transport.
!(assertEqual
  (formed-concept-same-intent-transports-to-ho
    False
    (formed-concept-mutual-inherits True True))
  False)
; And calibration cannot turn one-way formed inheritance into similarity.
!(assertEqual
  (formed-concept-same-intent-transports-to-ho
    True
    (formed-concept-mutual-inherits True False))
  False)
!(assertEqual
  (formed-concept-same-intent-assoc-pat-evidence-eq
    True (formed-concept-mutual-inherits True True)
    13.0 13.0 8.0 8.0)
  True)
; Equal score numbers are not enough without formed-concept mutual inheritance.
!(assertEqual
  (formed-concept-same-intent-assoc-pat-evidence-eq
    True (formed-concept-mutual-inherits True False)
    13.0 13.0 8.0 8.0)
  False)
; Same-intent is not enough if the claimed channel equalities are false.
!(assertEqual
  (formed-concept-same-intent-assoc-pat-evidence-eq
    True (formed-concept-mutual-inherits True True)
    13.0 12.0 8.0 8.0)
  False)
!(assertEqual
  (formed-concept-same-intent-mixed-evidence-eq
    True
    (formed-concept-same-intent-assoc-pat-evidence-eq
      True (formed-concept-mutual-inherits True True)
      13.0 13.0 8.0 8.0))
  True)
; Mixed evidence still needs the extensional channel to be separately equal.
!(assertEqual
  (formed-concept-same-intent-mixed-evidence-eq
    False
    (formed-concept-same-intent-assoc-pat-evidence-eq
      True (formed-concept-mutual-inherits True True)
      13.0 13.0 8.0 8.0))
  False)
!(assertEqual
  (formed-concept-table-ve-witness-eq-feature-witness 0 1)
  True)
!(assertEqual
  (formed-concept-table-assoc-ratio-numer 1 0 1 1)
  3)
!(assertEqual
  (formed-concept-table-assoc-ratio-denom 0 1 1)
  2)
; The formed-concept table inherits the same witness-only obstruction as the
; empirical and finite-table sources.
!(assertEqual
  (formed-concept-table-ve-witness-eq-feature-witness 1 1)
  False)
; The ASSOC ratio readout is not a substitute for the subset theorem: this
; witness-only table still has a 3/2 ratio surface while pair-subset is false.
!(assertEqual
  (formed-concept-table-assoc-ratio-numer 1 1 0 1)
  3)
!(assertEqual
  (formed-concept-table-assoc-ratio-denom 1 0 1)
  2)
!(assertEqual
  (formed-concept-calibration-transports-pair-subset-to-ho-intent
    True
    (formed-concept-table-ve-witness-eq-feature-witness 0 1))
  True)
; A formed-concept source fact still cannot enter the HO vocabulary unless the
; decoded predicate intent has been calibrated to that source interpretation.
!(assertEqual
  (formed-concept-calibration-transports-pair-subset-to-ho-intent
    False
    (formed-concept-table-ve-witness-eq-feature-witness 0 1))
  False)
!(assertEqual
  (generic-count-table-ve-witness-eq-feature-witness 1 1)
  True)
; Generic negative canary: when the witness marginal has support outside the
; chosen feature state, the witness marginal and joint query no longer agree.
!(assertEqual
  (generic-count-table-ve-witness-eq-feature-witness 2 1)
  False)
!(assertEqual
  (== (mixed-assoc-pat-sum-evidence 0.4 0.7 0.9)
      (mixed-assoc-pat-sum-evidence 0.4 0.7 0.9))
  True)
!(assertEqual
  (== (weighted-mixed-assoc-pat-sum-evidence
        4.0 2.0 3.0 1.0 2.0 2.0 1.0 4.0)
      (weighted-mixed-assoc-pat-sum-evidence
        4.0 2.0 3.0 1.0 2.0 2.0 1.0 4.0))
  True)
; Negative mixed-channel canary: same ASSOC/PAT values do not preserve mixed
; evidence when the extensional channel changes.
!(assertEqual
  (== (mixed-assoc-pat-sum-evidence 0.4 0.7 0.9)
      (mixed-assoc-pat-sum-evidence 0.5 0.7 0.9))
  False)
!(assertEqual
  (== (weighted-mixed-assoc-pat-sum-evidence
        4.0 2.0 3.0 1.0 2.0 2.0 1.0 4.0)
      (weighted-mixed-assoc-pat-sum-evidence
        5.0 2.0 3.0 1.0 2.0 2.0 1.0 4.0))
  False)

; Completeness-tightness canary: width tracks theoremic decidedness in the
; canonical family, not an arbitrary confidence penalty.
!(assertEqual (canonical-decided-query-width) 0.0)
!(assertEqual (canonical-open-query-width) 1.0)
!(assertEqual
  (== (canonical-decided-query-width)
      (canonical-open-query-width))
  False)

!(assertEqual (predicate-crisp-profile True) 1.0)
!(assertEqual (predicate-crisp-profile False) 0.0)
!(assertEqual (predicate-crisp-near-zero-profile True) 0.0)
!(assertEqual (predicate-crisp-near-zero-profile False) 1.0)
!(assertEqual (finite-two-near-one-fraction True True) 1.0)
!(assertEqual (finite-two-near-one-fraction True False) 0.5)
!(assertEqual (finite-two-near-zero-fraction True False) 0.5)
!(assertEqual (finite-two-near-zero-fraction True True) 0.0)
!(assertEqual (finite-two-qfm-forall 1.0 True True) True)
!(assertEqual (finite-two-qfm-forall 1.0 True False) False)
!(assertEqual (finite-two-qfm-forall 0.5 True False) True)
!(assertEqual (finite-two-qfm-there-exists 1.0 True False) False)
!(assertEqual (finite-two-qfm-there-exists 0.5 True False) True)
!(assertEqual (finite-two-qfm-there-exists 1.0 True True) True)
!(assertEqual (finite-two-qfm-interval-holds 0.5 0.5 True False) True)
!(assertEqual (finite-two-qfm-interval-holds 0.6 1.0 True False) False)
!(assertEqual (finite-two-qfm-interval-holds 0.0 1.0 True False) True)
!(assertEqual (finite-two-logical-forall True True) True)
!(assertEqual (finite-two-logical-forall True False) False)
!(assertEqual (finite-two-logical-exists True False) True)
; Strict QFM `ThereExists` at PCL=1 is not ordinary nonempty logical exists.
!(assertEqual (finite-two-qfm-there-exists 1.0 True False) False)
!(assertEqual (finite-two-inf-counting-near-one-mass True False) 0.5)
!(assertEqual (finite-two-inf-counting-near-zero-mass True False) 0.5)
!(assertEqual
  (== (finite-two-inf-counting-qfm-forall 0.5 True False)
      (finite-two-qfm-forall 0.5 True False))
  True)
!(assertEqual
  (== (finite-two-inf-counting-qfm-there-exists 0.5 True False)
      (finite-two-qfm-there-exists 0.5 True False))
  True)
!(assertEqual
  (== (finite-two-inf-counting-qfm-interval-holds 0.5 0.5 True False)
      (finite-two-qfm-interval-holds 0.5 0.5 True False))
  True)
!(assertEqual (finite-two-weighted-near-one-mass 0.8 0.2 True False) 0.8)
!(assertEqual (finite-two-weighted-near-zero-mass 0.8 0.2 True False) 0.2)
!(assertEqual (finite-two-weighted-extension-capacity 0.8 0.2 True False) 0.8)
!(assertEqual (finite-two-weighted-complement-extension-capacity 0.8 0.2 True False) 0.2)
!(assertEqual
  (== (finite-two-weighted-near-one-mass 0.8 0.2 True False)
      (finite-two-inf-counting-near-one-mass True False))
  False)
!(assertEqual (finite-two-weighted-qfm-forall 0.8 0.8 0.2 True False) True)
!(assertEqual (finite-two-weighted-qfm-forall 0.9 0.8 0.2 True False) False)
!(assertEqual (finite-two-weighted-qfm-there-exists 0.8 0.8 0.2 True False) True)
!(assertEqual (finite-two-weighted-qfm-there-exists 0.9 0.8 0.2 True False) False)
!(assertEqual
  (== (finite-two-weighted-qfm-there-exists-via-complement-capacity
        0.8 0.8 0.2 True False)
      (finite-two-weighted-qfm-there-exists 0.8 0.8 0.2 True False))
  True)
!(assertEqual
  (finite-two-weighted-qfm-there-exists-via-complement-capacity
    0.9 0.8 0.2 True False)
  False)
!(assertEqual
  (and (<= 0.19
           (finite-two-weighted-qfm-two-inh2sim-forall
             0.8 0.2 True False False True))
       (<= (finite-two-weighted-qfm-two-inh2sim-forall
             0.8 0.2 True False False True)
           0.20))
  True)
!(assertEqual
  (and (<= 0.19
           (finite-two-weighted-qfm-two-inh2sim-there-exists
             0.8 0.2 True False False True))
       (<= (finite-two-weighted-qfm-two-inh2sim-there-exists
             0.8 0.2 True False False True)
           0.20))
  True)
!(assertEqual
  (== (finite-two-weighted-qfm-two-inh2sim-forall
        0.8 0.2 True False False True)
      (finite-two-weighted-qfm-two-inh2sim-forall
        0.8 0.2 False True True False))
  True)
!(assertEqual (pln-modus-ponens 0.8 0.2 0.1) 0.24000000000000005)
!(assertEqual (pln-symmetric-modus-ponens 0.8 0.2 0.1) 0.30400000000000005)
!(assertEqual
  (and (<= 0.55 (pln-sim-to-inh 0.8 0.8 0.2))
       (<= (pln-sim-to-inh 0.8 0.8 0.2) 0.56))
  True)
!(assertEqual
  (<= (pln-sim-to-inh-hull-lower 0.4 0.8 0.2)
      (pln-sim-to-inh-hull-upper 0.8 0.6 0.4))
  True)
!(assertEqual
  (and (<= 0.23
           (finite-two-weighted-qfm-modus-ponens-forall
             0.1 0.8 0.2 True False False True))
       (<= (finite-two-weighted-qfm-modus-ponens-forall
             0.1 0.8 0.2 True False False True)
           0.25))
  True)
!(assertEqual
  (and (<= 0.23
           (finite-two-weighted-qfm-modus-ponens-there-exists
             0.1 0.8 0.2 True False False True))
       (<= (finite-two-weighted-qfm-modus-ponens-there-exists
             0.1 0.8 0.2 True False False True)
           0.25))
  True)
!(assertEqual
  (and (<= 0.30
           (finite-two-weighted-qfm-symmetric-modus-ponens-forall
             0.1 0.8 0.2 True False False True))
       (<= (finite-two-weighted-qfm-symmetric-modus-ponens-forall
             0.1 0.8 0.2 True False False True)
           0.31))
  True)
!(assertEqual
  (and (<= 0.30
           (finite-two-weighted-qfm-symmetric-modus-ponens-there-exists
             0.1 0.8 0.2 True False False True))
       (<= (finite-two-weighted-qfm-symmetric-modus-ponens-there-exists
             0.1 0.8 0.2 True False False True)
           0.31))
  True)
!(assertEqual
  (and (<= 0.55
           (finite-two-weighted-qfm-sim-to-inh-forall
             0.8 0.2 True False True False False True))
       (<= (finite-two-weighted-qfm-sim-to-inh-forall
             0.8 0.2 True False True False False True)
           0.56))
  True)
!(assertEqual
  (and (<= 0.55
           (finite-two-weighted-qfm-sim-to-inh-there-exists
             0.8 0.2 True False True False False True))
       (<= (finite-two-weighted-qfm-sim-to-inh-there-exists
             0.8 0.2 True False True False False True)
           0.56))
  True)
!(assertEqual (finite-two-weighted-qfm-interval-holds 0.75 0.85 0.8 0.2 True False) True)
!(assertEqual (finite-two-weighted-qfm-interval-holds 0.81 1.0 0.8 0.2 True False) False)
; Weighted-capacity monotonicity shadow: strengthening the predicate extension
; from `(True, False)` to `(True, True)` can only increase near-one mass.
!(assertEqual
  (<= (finite-two-weighted-near-one-mass 0.8 0.2 True False)
      (finite-two-weighted-near-one-mass 0.8 0.2 True True))
  True)
; Same-extension replacement shadow.
!(assertEqual
  (== (finite-two-weighted-qfm-forall 0.8 0.8 0.2 True False)
      (finite-two-weighted-qfm-forall 0.8 0.8 0.2 True False))
  True)

!(assertEqual (one-point-hierarchical-predicate-similarity True True) 1.0)
!(assertEqual (one-point-hierarchical-predicate-similarity True False) 0.0)
!(assertEqual (one-point-hierarchical-predicate-similarity False True) 0.0)
!(assertEqual (one-point-hierarchical-predicate-similarity False False) 0.0)

!(assertEqual (one-point-hierarchical-two-inh2sim-similarity True True) 1.0)
!(assertEqual (one-point-hierarchical-two-inh2sim-similarity True False) 0.0)
!(assertEqual (one-point-hierarchical-two-inh2sim-similarity False True) 0.0)
!(assertEqual (one-point-hierarchical-two-inh2sim-similarity False False) 0.0)

!(assertEqual (one-point-credal-predicate-similarity-lower True True) 1.0)
!(assertEqual (one-point-credal-predicate-similarity-upper True True) 1.0)
!(assertEqual (one-point-credal-predicate-similarity-lower True False) 0.0)
!(assertEqual (one-point-credal-predicate-similarity-upper True False) 0.0)
!(assertEqual
  (one-point-credal-predicate-similarity-itv-width True True 0.5)
  0.0)
!(assertEqual
  (one-point-credal-predicate-similarity-itv-width True False 0.5)
  0.0)
!(assertEqual
  (finite-two-product-multi-join 0.8 0.2)
  0.16000000000000003)
!(assertEqual
  (one-point-product-multi-join-itv-width 0.8 0.2 0.5)
  0.0)
!(assertEqual
  (and (<= 0.83 (finite-two-noisy-or-multi-join 0.8 0.2))
       (<= (finite-two-noisy-or-multi-join 0.8 0.2) 0.85))
  True)
!(assertEqual
  (one-point-noisy-or-multi-join-itv-width 0.8 0.2 0.5)
  0.0)
!(assertEqual
  (and (<= 0.999 (finite-two-product-complement-plus-noisy-or 0.8 0.2))
       (<= (finite-two-product-complement-plus-noisy-or 0.8 0.2) 1.001))
  True)
!(assertEqual
  (and (<= 0.83 (finite-two-noisy-or-complement-dual 0.8 0.2))
       (<= (finite-two-noisy-or-complement-dual 0.8 0.2) 0.85))
  True)
!(assertEqual
  (and (<= 0.83 (finite-two-one-sub-product-multi-join 0.8 0.2))
       (<= (finite-two-one-sub-product-multi-join 0.8 0.2) 0.85))
  True)

; Two-completion mirror of the Lean `[0,1]` canary:
; one completion has a nonempty domain where top/bot are not similar, while
; the other has an empty domain where both directed implications are vacuous.
!(assertEqual
  (two-completion-credal-predicate-similarity-lower False False True True)
  0.0)
!(assertEqual
  (two-completion-credal-predicate-similarity-upper False False True True)
  1.0)
!(assertEqual
  (two-completion-credal-predicate-similarity-lower False False False False)
  0.0)
!(assertEqual
  (two-completion-credal-predicate-similarity-upper False False False False)
  0.0)

; PLN ITV packaging mirror: endpoints come from the HO credal interval, while
; credibility is an external evidence-concentration coordinate.
!(assertEqual
  (two-completion-credal-predicate-similarity-itv-lower
    False False True True 0.5)
  0.0)
!(assertEqual
  (two-completion-credal-predicate-similarity-itv-upper
    False False True True 0.5)
  1.0)
!(assertEqual
  (two-completion-credal-predicate-similarity-itv-credibility
    False False True True 0.5)
  0.5)
!(assertEqual
  (two-completion-credal-predicate-similarity-itv-width
    False False True True 0.5)
  1.0)

; Same-completion PLN 2inh2sim mirror: combine the two directed implication
; strengths in each completion, then envelope the reconstructed similarity.
!(assertEqual
  (two-completion-two-inh2sim-predicate-similarity-lower False True True True)
  0.0)
!(assertEqual
  (two-completion-two-inh2sim-predicate-similarity-upper False True True True)
  1.0)
!(assertEqual
  (two-completion-two-inh2sim-predicate-similarity-itv-lower
    False True True True 0.5)
  0.0)
!(assertEqual
  (two-completion-two-inh2sim-predicate-similarity-itv-upper
    False True True True 0.5)
  1.0)
!(assertEqual
  (two-completion-two-inh2sim-predicate-similarity-itv-credibility
    False True True True 0.5)
  0.5)

; Same-completion binary rule mirror: apply a monotone product rule inside each
; completion, then envelope the resulting values.
!(assertEqual
  (two-completion-product-similarity-rule-lower False False True True)
  0.0)
!(assertEqual
  (two-completion-product-similarity-rule-upper False False True True)
  1.0)
!(assertEqual
  (two-completion-product-similarity-rule-itv-lower
    False False True True 0.5)
  0.0)
!(assertEqual
  (two-completion-product-similarity-rule-itv-upper
    False False True True 0.5)
  1.0)
!(assertEqual
  (two-completion-product-similarity-rule-itv-credibility
    False False True True 0.5)
  0.5)

; Indexed finite-rule mirror: the same two-completion discipline now works for
; a three-premise product rule, matching the Lean `Fin 3` canary.
!(assertEqual
  (two-completion-tri-product-similarity-rule-lower False False True True)
  0.0)
!(assertEqual
  (two-completion-tri-product-similarity-rule-upper False False True True)
  1.0)
!(assertEqual
  (two-completion-tri-product-similarity-rule-itv-lower
    False False True True 0.5)
  0.0)
!(assertEqual
  (two-completion-tri-product-similarity-rule-itv-upper
    False False True True 0.5)
  1.0)
!(assertEqual
  (two-completion-tri-product-similarity-rule-itv-credibility
    False False True True 0.5)
  0.5)
