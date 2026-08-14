!(import! &self ../../lib/lib_wmpln)

; Executable shadow of
; Mettapedia.Logic.PLNTypedSemanticLayerBridge.
;
; Semantic layer tags are gates into the existing inheritance channels.  They
; are not a new PLN semantics and they do not collapse intensional evidence into
; extensional membership.

(= (semantic-layer-channel EXTENSIONAL $method) EXTENSIONAL-CHANNEL)
(= (semantic-layer-channel MIXED $method) MIXED-CHANNEL)
(= (semantic-layer-channel PREEXTENSIONAL ASSOC) INTENSIONAL-ASSOC-CHANNEL)
(= (semantic-layer-channel PREEXTENSIONAL PAT) INTENSIONAL-PAT-CHANNEL)
(= (semantic-layer-channel INTENSIONAL ASSOC) INTENSIONAL-ASSOC-CHANNEL)
(= (semantic-layer-channel INTENSIONAL PAT) INTENSIONAL-PAT-CHANNEL)

; Query-level gate: the semantic layer selects the typed query constructor
; before evidence is extracted from the world model.
(= (semantic-layer-query EXTENSIONAL $method $a $b)
   (typed-query EXTENSIONAL-CHANNEL $a $b))
(= (semantic-layer-query MIXED $method $a $b)
   (typed-query MIXED-CHANNEL $a $b))
(= (semantic-layer-query PREEXTENSIONAL ASSOC $a $b)
   (typed-query INTENSIONAL-ASSOC-CHANNEL $a $b))
(= (semantic-layer-query PREEXTENSIONAL PAT $a $b)
   (typed-query INTENSIONAL-PAT-CHANNEL $a $b))
(= (semantic-layer-query INTENSIONAL ASSOC $a $b)
   (typed-query INTENSIONAL-ASSOC-CHANNEL $a $b))
(= (semantic-layer-query INTENSIONAL PAT $a $b)
   (typed-query INTENSIONAL-PAT-CHANNEL $a $b))

(= (typed-query-sort (typed-query $sort $a $b)) $sort)

; Tiny numeric evidence model for the four already-existing channels.  The
; concrete numbers are hand-chosen so each channel is distinguishable.
(= (channel-evidence EXTENSIONAL-CHANNEL $ext $assoc $pat $mixed) $ext)
(= (channel-evidence INTENSIONAL-ASSOC-CHANNEL $ext $assoc $pat $mixed) $assoc)
(= (channel-evidence INTENSIONAL-PAT-CHANNEL $ext $assoc $pat $mixed) $pat)
(= (channel-evidence MIXED-CHANNEL $ext $assoc $pat $mixed) $mixed)

(= (semantic-layer-evidence $layer $method $ext $assoc $pat $mixed)
   (channel-evidence
     (typed-query-sort (semantic-layer-query $layer $method source target))
     $ext $assoc $pat $mixed))

(= (semantic-layer-evidence-add
      $layer $method
      $ext1 $assoc1 $pat1 $mixed1
      $ext2 $assoc2 $pat2 $mixed2)
   (+ (semantic-layer-evidence $layer $method $ext1 $assoc1 $pat1 $mixed1)
      (semantic-layer-evidence $layer $method $ext2 $assoc2 $pat2 $mixed2)))

(= (semantic-layer-evidence-of-added-state
      $layer $method
      $ext1 $assoc1 $pat1 $mixed1
      $ext2 $assoc2 $pat2 $mixed2)
   (semantic-layer-evidence
      $layer $method
      (+ $ext1 $ext2)
      (+ $assoc1 $assoc2)
      (+ $pat1 $pat2)
      (+ $mixed1 $mixed2)))

; Positive routing canaries.
!(assertEqual (semantic-layer-channel EXTENSIONAL ASSOC) EXTENSIONAL-CHANNEL)
!(assertEqual (semantic-layer-channel EXTENSIONAL PAT) EXTENSIONAL-CHANNEL)
!(assertEqual (semantic-layer-channel MIXED PAT) MIXED-CHANNEL)
!(assertEqual (semantic-layer-channel PREEXTENSIONAL ASSOC) INTENSIONAL-ASSOC-CHANNEL)
!(assertEqual (semantic-layer-channel PREEXTENSIONAL PAT) INTENSIONAL-PAT-CHANNEL)
!(assertEqual (semantic-layer-channel INTENSIONAL ASSOC) INTENSIONAL-ASSOC-CHANNEL)
!(assertEqual (semantic-layer-channel INTENSIONAL PAT) INTENSIONAL-PAT-CHANNEL)
!(assertEqual
   (semantic-layer-query PREEXTENSIONAL ASSOC raven bird)
   (typed-query INTENSIONAL-ASSOC-CHANNEL raven bird))
!(assertEqual
   (typed-query-sort (semantic-layer-query INTENSIONAL PAT raven black))
   INTENSIONAL-PAT-CHANNEL)

; Negative routing canaries: the intensional tags do not collapse to the
; extensional channel.
!(assertEqual (== (semantic-layer-channel PREEXTENSIONAL ASSOC) EXTENSIONAL-CHANNEL) False)
!(assertEqual (== (semantic-layer-channel INTENSIONAL PAT) EXTENSIONAL-CHANNEL) False)
!(assertEqual
   (== (typed-query-sort (semantic-layer-query PREEXTENSIONAL ASSOC raven bird))
       EXTENSIONAL-CHANNEL)
   False)

; Concrete evidence selection over one state with ext=2, assoc=5, pat=7, mixed=13.
!(assertEqual (semantic-layer-evidence EXTENSIONAL ASSOC 2 5 7 13) 2)
!(assertEqual (semantic-layer-evidence PREEXTENSIONAL ASSOC 2 5 7 13) 5)
!(assertEqual (semantic-layer-evidence INTENSIONAL PAT 2 5 7 13) 7)
!(assertEqual (semantic-layer-evidence MIXED ASSOC 2 5 7 13) 13)

; Additivity is inherited from the selected channel.  Two states:
;   W1 = (2,5,7,13), W2 = (3,11,17,19)
; give ext=5, assoc=16, pat=24, mixed=32.
!(assertEqual
   (semantic-layer-evidence-add
     EXTENSIONAL ASSOC 2 5 7 13 3 11 17 19)
   (semantic-layer-evidence-of-added-state
     EXTENSIONAL ASSOC 2 5 7 13 3 11 17 19))
!(assertEqual
   (semantic-layer-evidence-add
     PREEXTENSIONAL ASSOC 2 5 7 13 3 11 17 19)
   16)
!(assertEqual
   (semantic-layer-evidence-of-added-state
     INTENSIONAL PAT 2 5 7 13 3 11 17 19)
   24)
!(assertEqual
   (semantic-layer-evidence-add
     MIXED PAT 2 5 7 13 3 11 17 19)
   32)

; Typed ASSOC/PAT consumer shadow:
; formed-concept same-intent + calibrated HO intent preserves the selected
; ASSOC/PAT channel for intensional-facing semantic layers.  The numbers below
; are hand-derived and intentionally keep extensional evidence distinct.

(= (semantic-layer-intensional-facing PREEXTENSIONAL) True)
(= (semantic-layer-intensional-facing INTENSIONAL) True)
(= (semantic-layer-intensional-facing EXTENSIONAL) False)
(= (semantic-layer-intensional-facing MIXED) False)

; Pair evidence profile: (ext, assoc, pat).  Mixed is the visible sum, so an
; extensional mismatch remains visible even when ASSOC/PAT agree.
(= (pair-ext WITNESS-SELF) 2)
(= (pair-assoc WITNESS-SELF) 11)
(= (pair-pat WITNESS-SELF) 17)

(= (pair-ext FEATURE-SELF) 3)
(= (pair-assoc FEATURE-SELF) 11)
(= (pair-pat FEATURE-SELF) 17)

(= (pair-ext WITNESS-ALT) 2)
(= (pair-assoc WITNESS-ALT) 11)
(= (pair-pat WITNESS-ALT) 17)

(= (pair-ext ONE-WAY) 2)
(= (pair-assoc ONE-WAY) 13)
(= (pair-pat ONE-WAY) 19)

(= (pair-ext MISSING-CALIBRATION) 2)
(= (pair-assoc MISSING-CALIBRATION) 11)
(= (pair-pat MISSING-CALIBRATION) 17)

; ASSOC/PAT increase while extensional evidence drops.  This is the negative
; mixed-channel monotonicity guardrail: intensional flow alone is not enough to
; move MIXED evidence.
(= (pair-ext HIGH-EXT-SOURCE) 10)
(= (pair-assoc HIGH-EXT-SOURCE) 11)
(= (pair-pat HIGH-EXT-SOURCE) 17)

(= (pair-ext LOW-EXT-TARGET) 0)
(= (pair-assoc LOW-EXT-TARGET) 13)
(= (pair-pat LOW-EXT-TARGET) 19)

(= (pair-mixed $pair)
   (+ (pair-ext $pair) (+ (pair-assoc $pair) (pair-pat $pair))))

; Deliberately bad non-cancellative mixed combiner: it drops the extensional
; coordinate, so it cannot recover extensional equality from mixed equality.
(= (pair-mixed-ignore-ext $pair)
   (+ (pair-assoc $pair) (pair-pat $pair)))

(= (semantic-layer-pair-evidence $layer $method $pair)
   (semantic-layer-evidence
     $layer $method
     (pair-ext $pair)
     (pair-assoc $pair)
     (pair-pat $pair)
     (pair-mixed $pair)))

; Formed-concept same-intent source canaries.  WITNESS-SELF and FEATURE-SELF
; have matching ASSOC/PAT channels but different extensional evidence.
(= (formed-same-intent WITNESS-SELF FEATURE-SELF) True)
(= (formed-same-intent WITNESS-SELF WITNESS-ALT) True)
(= (formed-same-intent WITNESS-SELF ONE-WAY) False)
(= (formed-same-intent WITNESS-SELF MISSING-CALIBRATION) True)

(= (calibrated-ho-intent WITNESS-SELF FEATURE-SELF) True)
(= (calibrated-ho-intent WITNESS-SELF WITNESS-ALT) True)
(= (calibrated-ho-intent WITNESS-SELF ONE-WAY) True)
(= (calibrated-ho-intent ONE-WAY WITNESS-SELF) True)
(= (calibrated-ho-intent WITNESS-SELF MISSING-CALIBRATION) False)
(= (calibrated-ho-intent HIGH-EXT-SOURCE LOW-EXT-TARGET) True)

(= (typed-semantic-layer-assoc-pat-eq
      $layer $left $right)
   (and (semantic-layer-intensional-facing $layer)
        (and (formed-same-intent $left $right)
             (and (calibrated-ho-intent $left $right)
                  (and
                    (== (semantic-layer-pair-evidence $layer ASSOC $left)
                        (semantic-layer-pair-evidence $layer ASSOC $right))
                    (== (semantic-layer-pair-evidence $layer PAT $left)
                        (semantic-layer-pair-evidence $layer PAT $right)))))))

(= (typed-semantic-layer-mixed-eq-with-ext
      $left $right)
   (and (formed-same-intent $left $right)
        (and (calibrated-ho-intent $left $right)
             (and
               (== (semantic-layer-pair-evidence EXTENSIONAL ASSOC $left)
                   (semantic-layer-pair-evidence EXTENSIONAL ASSOC $right))
               (== (semantic-layer-pair-evidence MIXED ASSOC $left)
                   (semantic-layer-pair-evidence MIXED ASSOC $right))))))

; Cancellative toy-combiner necessity shadow:
; mixed = ext + assoc + pat, so once ASSOC/PAT are already equal, mixed equality
; forces extensional equality.  This mirrors the Lean theorem
; semanticLayerExtensionalEvidence_eq_of_mixedEvidence_eq_... .
(= (typed-semantic-layer-cancellative-mixed-necessity
      $left $right)
   (and (formed-same-intent $left $right)
        (and (calibrated-ho-intent $left $right)
             (and
               (== (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC $left)
                   (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC $right))
               (and
                 (== (semantic-layer-pair-evidence INTENSIONAL PAT $left)
                     (semantic-layer-pair-evidence INTENSIONAL PAT $right))
                 (and
                   (== (semantic-layer-pair-evidence MIXED ASSOC $left)
                       (semantic-layer-pair-evidence MIXED ASSOC $right))
                   (== (semantic-layer-pair-evidence EXTENSIONAL ASSOC $left)
                       (semantic-layer-pair-evidence EXTENSIONAL ASSOC $right))))))))

; Cancellative toy-combiner iff shadow:
; with the same same-intent / equal ASSOC/PAT preconditions, mixed equality is
; equivalent to extensional equality.  This mirrors the Lean theorem
; semanticLayerMixedEvidence_eq_iff_extensionalEvidence_eq_... .
(= (typed-semantic-layer-cancellative-mixed-ext-iff
      $left $right)
   (and (formed-same-intent $left $right)
        (and (calibrated-ho-intent $left $right)
             (and
               (== (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC $left)
                   (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC $right))
               (and
                 (== (semantic-layer-pair-evidence INTENSIONAL PAT $left)
                     (semantic-layer-pair-evidence INTENSIONAL PAT $right))
                 (==
                   (== (semantic-layer-pair-evidence MIXED ASSOC $left)
                       (semantic-layer-pair-evidence MIXED ASSOC $right))
                   (== (semantic-layer-pair-evidence EXTENSIONAL ASSOC $left)
                       (semantic-layer-pair-evidence EXTENSIONAL ASSOC $right))))))))

; Formed-concept consumer of the same iff: the preconditions are the concrete
; formed same-intent and calibrated HO intent canaries, not a raw channel alias.
(= (formed-concept-calibrated-mixed-ext-iff-consumer
      $left $right)
   (typed-semantic-layer-cancellative-mixed-ext-iff $left $right))

; Finite-table Chapter-12 source algebra:
; the no-witness-only condition is load-bearing.  In the positive table,
; witnessOnly=0 and both=1, so witness marginal = witnessOnly+both = 1 and
; feature/witness joint = both = 1.  In the counterexample table, witnessOnly=1
; and both=1, so the witness marginal is 2 while the joint is 1.
(= (table-witness-only WITNESS-IMPLIES-FEATURE-TABLE) 0)
(= (table-both WITNESS-IMPLIES-FEATURE-TABLE) 1)
(= (table-witness-only WITNESS-ONLY-COUNTEREXAMPLE-TABLE) 1)
(= (table-both WITNESS-ONLY-COUNTEREXAMPLE-TABLE) 1)

(= (table-witness-marginal $table)
   (+ (table-witness-only $table) (table-both $table)))
(= (table-feature-witness-joint $table)
   (table-both $table))

(= (finite-table-no-witness-only-ve-readout $table)
   (and (== (table-witness-only $table) 0)
        (== (table-witness-marginal $table)
            (table-feature-witness-joint $table))))

(= (finite-table-witness-only-counterexample)
   (and (== (table-witness-only WITNESS-ONLY-COUNTEREXAMPLE-TABLE) 1)
        (== (==
              (table-witness-marginal WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
              (table-feature-witness-joint WITNESS-ONLY-COUNTEREXAMPLE-TABLE))
            False)))

; Pattern-coded Chapter-12 source syntax:
; concrete OSLF-style pattern names select the same finite-table source
; concepts.  The classifier is intentionally visible so the source syntax is
; not just an abstract MembershipConcept table.
(= (chapter12-pattern-concept CHAPTER12-FEATURE-PATTERN) FEATURE)
(= (chapter12-pattern-concept CHAPTER12-WITNESS-PATTERN) WITNESS)
(= (chapter12-pattern-concept CHAPTER12-UNKNOWN-PATTERN) FEATURE)

(= (pattern-coded-finite-table-pair-subset $featurePattern $witnessPattern $table)
   (and
     (== (chapter12-pattern-concept $featurePattern) FEATURE)
     (and
       (== (chapter12-pattern-concept $witnessPattern) WITNESS)
       (finite-table-no-witness-only-ve-readout $table))))

(= (pattern-coded-source-classifier-guardrail)
   (and
     (== (chapter12-pattern-concept CHAPTER12-FEATURE-PATTERN) FEATURE)
     (and
       (== (chapter12-pattern-concept CHAPTER12-WITNESS-PATTERN) WITNESS)
       (== (chapter12-pattern-concept CHAPTER12-FEATURE-PATTERN)
           (chapter12-pattern-concept CHAPTER12-WITNESS-PATTERN)))))

; Rich role-pattern Chapter-12 source syntax:
; the role wrapper carries the source concept channel while the payload remains
; available for richer OSLF syntax.  These are hand-derived shadows of
; chapter12RichPatternConcept_* and the RichPatternCoded* profiles in Lean.
(= (chapter12-rich-pattern-concept (chapter12-role-pattern FEATURE $payload)) FEATURE)
(= (chapter12-rich-pattern-concept (chapter12-role-pattern WITNESS $payload)) WITNESS)
(= (chapter12-rich-pattern-concept (chapter12-role-pattern DISTRACTOR $payload)) FEATURE)

(= (rich-pattern-feature-source)
   (chapter12-role-pattern FEATURE (observed CHAPTER12-FEATURE-PATTERN)))

(= (rich-pattern-witness-source)
   (chapter12-role-pattern WITNESS (observed CHAPTER12-WITNESS-PATTERN)))

(= (rich-pattern-coded-finite-table-pair-subset $featurePattern $witnessPattern $table)
   (and
     (== (chapter12-rich-pattern-concept $featurePattern) FEATURE)
     (and
       (== (chapter12-rich-pattern-concept $witnessPattern) WITNESS)
       (finite-table-no-witness-only-ve-readout $table))))

; Hand-derived positive values:
;   WITNESS-SELF ASSOC/PAT = (11,17)
;   FEATURE-SELF ASSOC/PAT = (11,17)
; so PREEXTENSIONAL/INTENSIONAL ASSOC/PAT are equal.
!(assertEqual
  (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC WITNESS-SELF)
  11)
!(assertEqual
  (semantic-layer-pair-evidence INTENSIONAL PAT FEATURE-SELF)
  17)
!(assertEqual
  (typed-semantic-layer-assoc-pat-eq
    PREEXTENSIONAL WITNESS-SELF FEATURE-SELF)
  True)
!(assertEqual
  (typed-semantic-layer-assoc-pat-eq
    INTENSIONAL WITNESS-SELF FEATURE-SELF)
  True)

; Negative guardrails:
;   EXTENSIONAL is not an intensional-facing layer.
;   one-way inheritance is not same-intent.
;   calibration remains load-bearing.
!(assertEqual
  (typed-semantic-layer-assoc-pat-eq
    EXTENSIONAL WITNESS-SELF FEATURE-SELF)
  False)
!(assertEqual
  (typed-semantic-layer-assoc-pat-eq
    PREEXTENSIONAL WITNESS-SELF ONE-WAY)
  False)
!(assertEqual
  (typed-semantic-layer-assoc-pat-eq
    PREEXTENSIONAL WITNESS-SELF MISSING-CALIBRATION)
  False)

; Mixed-channel guardrail:
; WITNESS-SELF mixed = 2 + 11 + 17 = 30.
; FEATURE-SELF mixed = 3 + 11 + 17 = 31.
; Thus same-intent preserves ASSOC/PAT, but not mixed evidence unless
; extensional evidence is also equal. WITNESS-ALT has ext=2, so mixed matches.
!(assertEqual (semantic-layer-pair-evidence MIXED ASSOC WITNESS-SELF) 30)
!(assertEqual (semantic-layer-pair-evidence MIXED ASSOC FEATURE-SELF) 31)
!(assertEqual
  (typed-semantic-layer-mixed-eq-with-ext WITNESS-SELF FEATURE-SELF)
  False)
!(assertEqual
  (== (semantic-layer-pair-evidence MIXED ASSOC WITNESS-SELF)
      (semantic-layer-pair-evidence MIXED ASSOC FEATURE-SELF))
  False)
!(assertEqual
  (typed-semantic-layer-mixed-eq-with-ext WITNESS-SELF WITNESS-ALT)
  True)
!(assertEqual
  (typed-semantic-layer-cancellative-mixed-necessity WITNESS-SELF WITNESS-ALT)
  True)
!(assertEqual
  (== (semantic-layer-pair-evidence EXTENSIONAL ASSOC WITNESS-SELF)
      (semantic-layer-pair-evidence EXTENSIONAL ASSOC WITNESS-ALT))
  True)
!(assertEqual
  (typed-semantic-layer-cancellative-mixed-necessity WITNESS-SELF FEATURE-SELF)
  False)
!(assertEqual
  (== (semantic-layer-pair-evidence EXTENSIONAL ASSOC WITNESS-SELF)
      (semantic-layer-pair-evidence EXTENSIONAL ASSOC FEATURE-SELF))
  False)
!(assertEqual (pair-mixed-ignore-ext WITNESS-SELF) 28)
!(assertEqual (pair-mixed-ignore-ext FEATURE-SELF) 28)
!(assertEqual
  (== (pair-mixed-ignore-ext WITNESS-SELF)
      (pair-mixed-ignore-ext FEATURE-SELF))
  True)
!(assertEqual
  (typed-semantic-layer-cancellative-mixed-ext-iff WITNESS-SELF WITNESS-ALT)
  True)
!(assertEqual
  (typed-semantic-layer-cancellative-mixed-ext-iff WITNESS-SELF FEATURE-SELF)
  True)
!(assertEqual
  (typed-semantic-layer-cancellative-mixed-ext-iff WITNESS-SELF ONE-WAY)
  False)
!(assertEqual
  (formed-concept-calibrated-mixed-ext-iff-consumer WITNESS-SELF WITNESS-ALT)
  True)
!(assertEqual
  (formed-concept-calibrated-mixed-ext-iff-consumer WITNESS-SELF FEATURE-SELF)
  True)
!(assertEqual
  (formed-concept-calibrated-mixed-ext-iff-consumer WITNESS-SELF MISSING-CALIBRATION)
  False)

; Finite-table source positives and negative guardrail:
;   WITNESS-IMPLIES-FEATURE-TABLE has no witness-only cases, so marginal=joint.
;   WITNESS-ONLY-COUNTEREXAMPLE-TABLE has one witness-only case, so they differ.
!(assertEqual
  (table-witness-marginal WITNESS-IMPLIES-FEATURE-TABLE)
  1)
!(assertEqual
  (table-feature-witness-joint WITNESS-IMPLIES-FEATURE-TABLE)
  1)
!(assertEqual
  (finite-table-no-witness-only-ve-readout WITNESS-IMPLIES-FEATURE-TABLE)
  True)
!(assertEqual
  (table-witness-marginal WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
  2)
!(assertEqual
  (table-feature-witness-joint WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
  1)
!(assertEqual
  (== (table-witness-marginal WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
      (table-feature-witness-joint WITNESS-ONLY-COUNTEREXAMPLE-TABLE))
  False)
!(assertEqual
  (finite-table-witness-only-counterexample)
  True)

; Pattern-coded finite-table source positives and guardrails:
;   feature-pattern -> FEATURE, witness-pattern -> WITNESS, and the positive
;   table has marginal=joint.  Reversing the patterns or using the
;   witness-only table fails for hand-derived reasons.
!(assertEqual
  (chapter12-pattern-concept CHAPTER12-FEATURE-PATTERN)
  FEATURE)
!(assertEqual
  (chapter12-pattern-concept CHAPTER12-WITNESS-PATTERN)
  WITNESS)
!(assertEqual
  (== (chapter12-pattern-concept CHAPTER12-FEATURE-PATTERN)
      (chapter12-pattern-concept CHAPTER12-WITNESS-PATTERN))
  False)
!(assertEqual
  (pattern-coded-finite-table-pair-subset
    CHAPTER12-FEATURE-PATTERN
    CHAPTER12-WITNESS-PATTERN
    WITNESS-IMPLIES-FEATURE-TABLE)
  True)
!(assertEqual
  (pattern-coded-finite-table-pair-subset
    CHAPTER12-WITNESS-PATTERN
    CHAPTER12-FEATURE-PATTERN
    WITNESS-IMPLIES-FEATURE-TABLE)
  False)
!(assertEqual
  (pattern-coded-finite-table-pair-subset
    CHAPTER12-FEATURE-PATTERN
    CHAPTER12-WITNESS-PATTERN
    WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
  False)
!(assertEqual
  (pattern-coded-source-classifier-guardrail)
  False)

; Rich role-pattern positives and noncollapse guardrails:
; feature/witness roles classify by role, an unknown role falls back to feature,
; and the three negative classifier facts are load-bearing.
!(assertEqual
  (chapter12-rich-pattern-concept
    (chapter12-role-pattern FEATURE (observed CHAPTER12-FEATURE-PATTERN)))
  FEATURE)
!(assertEqual
  (chapter12-rich-pattern-concept
    (chapter12-role-pattern WITNESS (observed CHAPTER12-WITNESS-PATTERN)))
  WITNESS)
!(assertEqual
  (chapter12-rich-pattern-concept
    (chapter12-role-pattern DISTRACTOR (observed CHAPTER12-UNKNOWN-PATTERN)))
  FEATURE)
!(assertEqual
  (== (chapter12-rich-pattern-concept
        (chapter12-role-pattern FEATURE (observed CHAPTER12-FEATURE-PATTERN)))
      WITNESS)
  False)
!(assertEqual
  (== (chapter12-rich-pattern-concept
        (chapter12-role-pattern WITNESS (observed CHAPTER12-WITNESS-PATTERN)))
      FEATURE)
  False)
!(assertEqual
  (== (chapter12-rich-pattern-concept
        (chapter12-role-pattern DISTRACTOR (observed CHAPTER12-UNKNOWN-PATTERN)))
      WITNESS)
  False)
!(assertEqual
  (== (rich-pattern-feature-source) (rich-pattern-witness-source))
  False)
!(assertEqual
  (rich-pattern-coded-finite-table-pair-subset
    (rich-pattern-feature-source)
    (rich-pattern-witness-source)
    WITNESS-IMPLIES-FEATURE-TABLE)
  True)
!(assertEqual
  (rich-pattern-coded-finite-table-pair-subset
    (rich-pattern-witness-source)
    (rich-pattern-feature-source)
    WITNESS-IMPLIES-FEATURE-TABLE)
  False)
!(assertEqual
  (rich-pattern-coded-finite-table-pair-subset
    (rich-pattern-feature-source)
    (rich-pattern-witness-source)
    WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
  False)

; Typed semantic-layer monotonicity shadow:
; A formed-concept / calibrated HO pair-subset relation should move ASSOC and
; PAT evidence upward through intensional-facing layer tags.  This mirrors
;   semanticLayerAssocPatEvidence_mono_of_predicateVocabularyWeightedPairOrderRankScore
; and is one-way: unlike same-intent, it does not assert equality.
(= (leq $x $y) (or (< $x $y) (== $x $y)))

(= (formed-pair-subset WITNESS-SELF ONE-WAY) True)
(= (formed-pair-subset ONE-WAY WITNESS-SELF) False)
(= (formed-pair-subset WITNESS-SELF MISSING-CALIBRATION) True)
(= (formed-pair-subset HIGH-EXT-SOURCE LOW-EXT-TARGET) True)

(= (typed-semantic-layer-assoc-pat-mono
      $layer $left $right)
   (and (semantic-layer-intensional-facing $layer)
        (and (formed-pair-subset $left $right)
             (and (calibrated-ho-intent $left $right)
                  (and
                    (leq (semantic-layer-pair-evidence $layer ASSOC $left)
                         (semantic-layer-pair-evidence $layer ASSOC $right))
                    (leq (semantic-layer-pair-evidence $layer PAT $left)
                         (semantic-layer-pair-evidence $layer PAT $right)))))))

; Pattern-coded finite-table source consumer shadow:
; the OSLF-style pattern source must classify to feature/witness and have the
; no-witness-only finite-table readout before it can feed the existing typed
; semantic-layer ASSOC/PAT monotonicity witness.
(= (pattern-coded-source-semantic-layer-assoc-pat-mono
      $layer $left $right $table)
   (and
     (pattern-coded-finite-table-pair-subset
       CHAPTER12-FEATURE-PATTERN CHAPTER12-WITNESS-PATTERN $table)
     (typed-semantic-layer-assoc-pat-mono $layer $left $right)))

; Rich role-pattern consumer shadow:
; this is the same typed ASSOC/PAT monotonicity theorem as above, but its source
; classifier consumes role-wrapped patterns with payloads.
(= (rich-pattern-coded-source-semantic-layer-assoc-pat-mono
      $layer $left $right $table)
   (and
     (rich-pattern-coded-finite-table-pair-subset
       (rich-pattern-feature-source) (rich-pattern-witness-source) $table)
     (typed-semantic-layer-assoc-pat-mono $layer $left $right)))

; Direct formed-source ASSOC/PAT consumer shadow:
;   witnessFeature_semanticLayerAssocPatEvidence_mono_viaFormedConceptCalibration
(= (formed-source-semantic-layer-assoc-pat-mono
      $layer $left $right)
   (typed-semantic-layer-assoc-pat-mono $layer $left $right))

; Mixed-channel monotonicity additionally needs the extensional channel to be
; monotone.  This mirrors
;   semanticLayerMixedEvidence_mono_of_predicateVocabularyWeightedPairOrderRankScore
; with an explicit monotone mixed combiner assumption; the toy combiner is sum.
(= (typed-semantic-layer-mixed-mono-with-ext
      $left $right)
   (and (formed-pair-subset $left $right)
        (and (calibrated-ho-intent $left $right)
             (and
               (leq (semantic-layer-pair-evidence EXTENSIONAL ASSOC $left)
                    (semantic-layer-pair-evidence EXTENSIONAL ASSOC $right))
               (and
                 (leq (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC $left)
                      (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC $right))
                 (and
                   (leq (semantic-layer-pair-evidence INTENSIONAL PAT $left)
                        (semantic-layer-pair-evidence INTENSIONAL PAT $right))
                   (leq (semantic-layer-pair-evidence MIXED ASSOC $left)
                        (semantic-layer-pair-evidence MIXED ASSOC $right))))))))

; Direct formed-source consumer shadow:
;   witnessFeature_semanticLayerMixedEvidence_mono_viaFormedConceptCalibration
; It consumes the same formed-pair source relation as the ASSOC/PAT theorem,
; but can move MIXED evidence only under the explicit extensional/combiner
; monotonicity checks above.
(= (formed-source-semantic-layer-mixed-mono $left $right)
   (typed-semantic-layer-mixed-mono-with-ext $left $right))

; Direct counterexample shadow for
;   mixedEvidence_mono_requires_extensional_mono_counterexample.
; ASSOC/PAT move upward from (11,17) to (13,19), but extensional evidence drops
; from 10 to 0, so the sum-mixed channel drops from 38 to 32.
(= (mixed-mono-requires-ext-counterexample)
   (and
     (typed-semantic-layer-assoc-pat-mono
       PREEXTENSIONAL HIGH-EXT-SOURCE LOW-EXT-TARGET)
     (and
       (== (leq
             (semantic-layer-pair-evidence EXTENSIONAL ASSOC HIGH-EXT-SOURCE)
             (semantic-layer-pair-evidence EXTENSIONAL ASSOC LOW-EXT-TARGET))
           False)
       (== (leq
             (semantic-layer-pair-evidence MIXED ASSOC HIGH-EXT-SOURCE)
             (semantic-layer-pair-evidence MIXED ASSOC LOW-EXT-TARGET))
           False))))

; Positive one-way concept-geometry flow:
;   WITNESS-SELF ASSOC/PAT = (11,17)
;   ONE-WAY      ASSOC/PAT = (13,19)
; so the intensional-facing channels are monotone, but not equal.
!(assertEqual
  (leq (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC WITNESS-SELF)
       (semantic-layer-pair-evidence PREEXTENSIONAL ASSOC ONE-WAY))
  True)
!(assertEqual
  (leq (semantic-layer-pair-evidence INTENSIONAL PAT WITNESS-SELF)
       (semantic-layer-pair-evidence INTENSIONAL PAT ONE-WAY))
  True)
!(assertEqual
  (typed-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF ONE-WAY)
  True)
!(assertEqual
  (formed-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF ONE-WAY)
  True)
!(assertEqual
  (pattern-coded-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF ONE-WAY WITNESS-IMPLIES-FEATURE-TABLE)
  True)
!(assertEqual
  (rich-pattern-coded-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF ONE-WAY WITNESS-IMPLIES-FEATURE-TABLE)
  True)
!(assertEqual
  (typed-semantic-layer-assoc-pat-eq
    PREEXTENSIONAL WITNESS-SELF ONE-WAY)
  False)
; With the toy mixed combiner as sum, ext=2<=2 and ASSOC/PAT=(11,17)<=(13,19)
; give mixed 30<=34.
!(assertEqual (semantic-layer-pair-evidence MIXED ASSOC ONE-WAY) 34)
!(assertEqual
  (typed-semantic-layer-mixed-mono-with-ext WITNESS-SELF ONE-WAY)
  True)
!(assertEqual
  (formed-source-semantic-layer-mixed-mono WITNESS-SELF ONE-WAY)
  True)

; Negative guardrails: monotonicity is typed-channel and source-calibration
; sensitive.  It fails for extensional tags, reversed source order, and missing
; calibrated HO intent.
!(assertEqual
  (typed-semantic-layer-assoc-pat-mono
    EXTENSIONAL WITNESS-SELF ONE-WAY)
  False)
!(assertEqual
  (formed-source-semantic-layer-assoc-pat-mono
    EXTENSIONAL WITNESS-SELF ONE-WAY)
  False)
!(assertEqual
  (pattern-coded-source-semantic-layer-assoc-pat-mono
    EXTENSIONAL WITNESS-SELF ONE-WAY WITNESS-IMPLIES-FEATURE-TABLE)
  False)
!(assertEqual
  (rich-pattern-coded-source-semantic-layer-assoc-pat-mono
    EXTENSIONAL WITNESS-SELF ONE-WAY WITNESS-IMPLIES-FEATURE-TABLE)
  False)
!(assertEqual
  (typed-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL ONE-WAY WITNESS-SELF)
  False)
!(assertEqual
  (pattern-coded-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL ONE-WAY WITNESS-SELF WITNESS-IMPLIES-FEATURE-TABLE)
  False)
!(assertEqual
  (rich-pattern-coded-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL ONE-WAY WITNESS-SELF WITNESS-IMPLIES-FEATURE-TABLE)
  False)
!(assertEqual
  (typed-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF MISSING-CALIBRATION)
  False)
!(assertEqual
  (formed-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF MISSING-CALIBRATION)
  False)
!(assertEqual
  (pattern-coded-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF ONE-WAY WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
  False)
!(assertEqual
  (rich-pattern-coded-source-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL WITNESS-SELF ONE-WAY WITNESS-ONLY-COUNTEREXAMPLE-TABLE)
  False)

; ASSOC/PAT-only monotonicity can still hold while MIXED monotonicity fails if
; the extensional component drops.  Here ASSOC/PAT=(11,17)<=(13,19), but
; extensional evidence drops from 10 to 0 and mixed evidence drops 38 -> 32.
!(assertEqual
  (typed-semantic-layer-assoc-pat-mono
    PREEXTENSIONAL HIGH-EXT-SOURCE LOW-EXT-TARGET)
  True)
!(assertEqual
  (leq (semantic-layer-pair-evidence EXTENSIONAL ASSOC HIGH-EXT-SOURCE)
       (semantic-layer-pair-evidence EXTENSIONAL ASSOC LOW-EXT-TARGET))
  False)
!(assertEqual
  (leq (semantic-layer-pair-evidence MIXED ASSOC HIGH-EXT-SOURCE)
       (semantic-layer-pair-evidence MIXED ASSOC LOW-EXT-TARGET))
  False)
!(assertEqual
  (typed-semantic-layer-mixed-mono-with-ext HIGH-EXT-SOURCE LOW-EXT-TARGET)
  False)
!(assertEqual
  (formed-source-semantic-layer-mixed-mono HIGH-EXT-SOURCE LOW-EXT-TARGET)
  False)
!(assertEqual
  (mixed-mono-requires-ext-counterexample)
  True)

; Semantic-layer formed-concept closure shadow:
; mirrors PLNTypedSemanticLayerConceptClosureBridge.lean.  Robust lower
; concepts and permissive upper concepts live in different carriers, but the
; encoder is compatible because both carriers expose the same intent ids.
; Thus lower semantic-layer seeds transport to upper seeds; determined upper
; seeds transport back to lower seeds; and threshold/admissibility consumers
; read the same hand-derived full-inheritance scores.

(= (formed-concept-intent ROBUST-WITNESS) WITNESS-INTENT)
(= (formed-concept-intent ROBUST-FEATURE) FEATURE-INTENT)
(= (formed-concept-intent ROBUST-OPEN) OPEN-INTENT)
(= (formed-concept-intent PERMISSIVE-WITNESS) WITNESS-INTENT)
(= (formed-concept-intent PERMISSIVE-FEATURE) FEATURE-INTENT)
(= (formed-concept-intent PERMISSIVE-OPEN) OPEN-INTENT)

(= (lower-pair-source LOWER-WITNESS-FEATURE) ROBUST-WITNESS)
(= (lower-pair-target LOWER-WITNESS-FEATURE) ROBUST-FEATURE)
(= (lower-pair-source LOWER-OPEN) ROBUST-OPEN)
(= (lower-pair-target LOWER-OPEN) ROBUST-FEATURE)

(= (upper-pair-source UPPER-WITNESS-FEATURE) PERMISSIVE-WITNESS)
(= (upper-pair-target UPPER-WITNESS-FEATURE) PERMISSIVE-FEATURE)
(= (upper-pair-source UPPER-OPEN) PERMISSIVE-OPEN)
(= (upper-pair-target UPPER-OPEN) PERMISSIVE-FEATURE)

(= (formed-lower-seed LOWER-WITNESS-FEATURE) True)
(= (formed-lower-seed LOWER-OPEN) False)
(= (formed-upper-seed UPPER-WITNESS-FEATURE) True)
(= (formed-upper-seed UPPER-OPEN) True)

(= (lower-to-upper-pair LOWER-WITNESS-FEATURE) UPPER-WITNESS-FEATURE)
(= (lower-to-upper-pair LOWER-OPEN) UPPER-OPEN)
(= (upper-to-lower-pair UPPER-WITNESS-FEATURE) LOWER-WITNESS-FEATURE)
(= (upper-to-lower-pair UPPER-OPEN) LOWER-OPEN)

(= (projectively-determined-upper UPPER-WITNESS-FEATURE) True)
(= (projectively-determined-upper UPPER-OPEN) False)

(= (semantic-layer-formed-query $layer $method $source $target)
   (semantic-layer-query
     $layer $method
     (formed-concept-intent $source)
     (formed-concept-intent $target)))

(= (semantic-layer-lower-formed-query $layer $method $pair)
   (semantic-layer-formed-query
     $layer $method
     (lower-pair-source $pair)
     (lower-pair-target $pair)))

(= (semantic-layer-upper-formed-query $layer $method $pair)
   (semantic-layer-formed-query
     $layer $method
     (upper-pair-source $pair)
     (upper-pair-target $pair)))

(= (lower-upper-encoder-compatible $layer $method $pair)
   (==
     (semantic-layer-lower-formed-query $layer $method $pair)
     (semantic-layer-upper-formed-query
       $layer $method (lower-to-upper-pair $pair))))

(= (upper-lower-encoder-compatible $layer $method $pair)
   (==
     (semantic-layer-upper-formed-query $layer $method $pair)
     (semantic-layer-lower-formed-query
       $layer $method (upper-to-lower-pair $pair))))

(= (lower-semantic-layer-query-member $layer $method $pair)
   (and (semantic-layer-intensional-facing $layer)
        (formed-lower-seed $pair)))

(= (upper-semantic-layer-query-member $layer $method $pair)
   (and (semantic-layer-intensional-facing $layer)
        (formed-upper-seed $pair)))

(= (lower-seed-embeds-upper-semantic-layer $layer $method $pair)
   (and
     (lower-semantic-layer-query-member $layer $method $pair)
     (and
       (lower-upper-encoder-compatible $layer $method $pair)
       (upper-semantic-layer-query-member
         $layer $method (lower-to-upper-pair $pair)))))

(= (determined-upper-seed-embeds-lower-semantic-layer $layer $method $pair)
   (and
     (upper-semantic-layer-query-member $layer $method $pair)
     (and
       (projectively-determined-upper $pair)
       (and
         (upper-lower-encoder-compatible $layer $method $pair)
         (lower-semantic-layer-query-member
           $layer $method (upper-to-lower-pair $pair))))))

; Hand-derived full-inheritance scores used by the threshold and
; WM-admissibility readouts below.  These are semantic envelope witnesses, not
; numeric proof-theoretic tightness claims.
(= (full-inheritance-score LOWER-WITNESS-FEATURE) 0.8)
(= (full-inheritance-score LOWER-OPEN) 0.4)
(= (full-inheritance-score UPPER-WITNESS-FEATURE) 0.7)
(= (full-inheritance-score UPPER-OPEN) 0.5)

(= (threshold-valid-lower-semantic-layer $layer $method $tau $pair)
   (and
     (lower-semantic-layer-query-member $layer $method $pair)
     (leq $tau (full-inheritance-score $pair))))

(= (threshold-valid-upper-semantic-layer $layer $method $tau $pair)
   (and
     (upper-semantic-layer-query-member $layer $method $pair)
     (leq $tau (full-inheritance-score $pair))))

(= (available-region AVAILABLE-ROBUST) True)
(= (available-region AVAILABLE-PERMISSIVE) True)
(= (available-covered-by-lower-closure AVAILABLE-ROBUST LOWER-WITNESS-FEATURE) True)
(= (available-covered-by-lower-closure AVAILABLE-ROBUST LOWER-OPEN) False)
(= (available-covered-by-upper-closure AVAILABLE-PERMISSIVE UPPER-WITNESS-FEATURE) True)
(= (available-covered-by-upper-closure AVAILABLE-PERMISSIVE UPPER-OPEN) True)

(= (wm-admissible-lower-semantic-region $layer $method $region $tau $pair)
   (and
     (available-region $region)
     (and
       (available-covered-by-lower-closure $region $pair)
       (threshold-valid-lower-semantic-layer $layer $method $tau $pair))))

(= (wm-admissible-upper-semantic-region $layer $method $region $tau $pair)
   (and
     (available-region $region)
     (and
       (available-covered-by-upper-closure $region $pair)
       (threshold-valid-upper-semantic-layer $layer $method $tau $pair))))

; Lower-to-upper transport positives and guardrails.
!(assertEqual
  (lower-upper-encoder-compatible
    PREEXTENSIONAL ASSOC LOWER-WITNESS-FEATURE)
  True)
!(assertEqual
  (lower-seed-embeds-upper-semantic-layer
    PREEXTENSIONAL ASSOC LOWER-WITNESS-FEATURE)
  True)
!(assertEqual
  (lower-seed-embeds-upper-semantic-layer
    EXTENSIONAL ASSOC LOWER-WITNESS-FEATURE)
  False)
!(assertEqual
  (lower-seed-embeds-upper-semantic-layer
    PREEXTENSIONAL ASSOC LOWER-OPEN)
  False)

; Determined upper-to-lower collapse positives and guardrails.
!(assertEqual
  (upper-lower-encoder-compatible
    INTENSIONAL PAT UPPER-WITNESS-FEATURE)
  True)
!(assertEqual
  (determined-upper-seed-embeds-lower-semantic-layer
    INTENSIONAL PAT UPPER-WITNESS-FEATURE)
  True)
!(assertEqual
  (determined-upper-seed-embeds-lower-semantic-layer
    INTENSIONAL PAT UPPER-OPEN)
  False)

; Exact full-inheritance threshold consumers, before/after the least-closure
; wrapper in Lean.  The example keeps only the hand-derived threshold boundary.
!(assertEqual
  (threshold-valid-lower-semantic-layer
    PREEXTENSIONAL ASSOC 0.75 LOWER-WITNESS-FEATURE)
  True)
!(assertEqual
  (threshold-valid-lower-semantic-layer
    PREEXTENSIONAL ASSOC 0.85 LOWER-WITNESS-FEATURE)
  False)
!(assertEqual
  (threshold-valid-upper-semantic-layer
    INTENSIONAL PAT 0.65 UPPER-WITNESS-FEATURE)
  True)
!(assertEqual
  (threshold-valid-upper-semantic-layer
    INTENSIONAL PAT 0.75 UPPER-WITNESS-FEATURE)
  False)

; WM-admissible-region consumers: coverage by the semantic-layer closure plus
; the threshold-valid readout admits the available region; missing coverage or
; too-high thresholds block it.
!(assertEqual
  (wm-admissible-lower-semantic-region
    PREEXTENSIONAL ASSOC AVAILABLE-ROBUST 0.75 LOWER-WITNESS-FEATURE)
  True)
!(assertEqual
  (wm-admissible-lower-semantic-region
    PREEXTENSIONAL ASSOC AVAILABLE-ROBUST 0.75 LOWER-OPEN)
  False)
!(assertEqual
  (wm-admissible-upper-semantic-region
    INTENSIONAL PAT AVAILABLE-PERMISSIVE 0.65 UPPER-WITNESS-FEATURE)
  True)
!(assertEqual
  (wm-admissible-upper-semantic-region
    INTENSIONAL PAT AVAILABLE-PERMISSIVE 0.75 UPPER-WITNESS-FEATURE)
  False)

; Concrete abstract-inheritance demo shadow:
; mirrors the bird/penguin worked example in
; AbstractInheritanceIntensionalDemo.lean.  The consequent is still BIRD, so
; extensional evidence is unchanged; the more specific PENGUIN antecedent raises
; ASSOC/PAT evidence, hence the mixed sum also rises.  This is a semantic-layer
; evidence witness, not a numeric tightness claim.
(= (pair-ext BIRD-BIRD) 2)
(= (pair-assoc BIRD-BIRD) 11)
(= (pair-pat BIRD-BIRD) 17)

(= (pair-ext PENGUIN-BIRD) 2)
(= (pair-assoc PENGUIN-BIRD) 13)
(= (pair-pat PENGUIN-BIRD) 19)

(= (formed-pair-subset BIRD-BIRD PENGUIN-BIRD) True)
(= (formed-pair-subset PENGUIN-BIRD BIRD-BIRD) False)
(= (calibrated-ho-intent BIRD-BIRD PENGUIN-BIRD) True)
(= (calibrated-ho-intent PENGUIN-BIRD BIRD-BIRD) True)

; Stamped-provenance guardrail: the same abstract-inheritance witness should
; not be revised with itself as if it were independent evidence.
(= (abstract-demo-shared-provenance BIRD-BIRD-EVIDENCE PENGUIN-BIRD-EVIDENCE) True)
(= (abstract-demo-shared-provenance PENGUIN-BIRD-EVIDENCE BIRD-BIRD-EVIDENCE) True)
(= (abstract-demo-shared-provenance PENGUIN-BIRD-EVIDENCE DISTINCT-EVIDENCE) False)

(= (abstract-demo-guarded-revise BIRD-BIRD-EVIDENCE PENGUIN-BIRD-EVIDENCE) NONE)
(= (abstract-demo-guarded-revise PENGUIN-BIRD-EVIDENCE BIRD-BIRD-EVIDENCE) NONE)
(= (abstract-demo-guarded-revise PENGUIN-BIRD-EVIDENCE DISTINCT-EVIDENCE) SOME)

(= (abstract-demo-semantic-layer-canary)
   (and
     (typed-semantic-layer-assoc-pat-mono INTENSIONAL BIRD-BIRD PENGUIN-BIRD)
     (and
       (typed-semantic-layer-mixed-mono-with-ext BIRD-BIRD PENGUIN-BIRD)
       (and
         (==
           (==
             (semantic-layer-pair-evidence INTENSIONAL ASSOC BIRD-BIRD)
             (semantic-layer-pair-evidence INTENSIONAL PAT BIRD-BIRD))
           False)
         (==
           (abstract-demo-guarded-revise
             PENGUIN-BIRD-EVIDENCE BIRD-BIRD-EVIDENCE)
           NONE)))))

!(assertEqual
  (leq
    (semantic-layer-pair-evidence INTENSIONAL ASSOC BIRD-BIRD)
    (semantic-layer-pair-evidence INTENSIONAL ASSOC PENGUIN-BIRD))
  True)
!(assertEqual
  (leq
    (semantic-layer-pair-evidence INTENSIONAL PAT BIRD-BIRD)
    (semantic-layer-pair-evidence INTENSIONAL PAT PENGUIN-BIRD))
  True)
!(assertEqual
  (semantic-layer-pair-evidence MIXED ASSOC BIRD-BIRD)
  30)
!(assertEqual
  (semantic-layer-pair-evidence MIXED ASSOC PENGUIN-BIRD)
  34)
!(assertEqual
  (typed-semantic-layer-assoc-pat-mono
    INTENSIONAL BIRD-BIRD PENGUIN-BIRD)
  True)
!(assertEqual
  (typed-semantic-layer-mixed-mono-with-ext
    BIRD-BIRD PENGUIN-BIRD)
  True)
!(assertEqual
  (typed-semantic-layer-mixed-mono-with-ext
    PENGUIN-BIRD BIRD-BIRD)
  False)
!(assertEqual
  (==
    (semantic-layer-pair-evidence INTENSIONAL ASSOC BIRD-BIRD)
    (semantic-layer-pair-evidence INTENSIONAL PAT BIRD-BIRD))
  False)
!(assertEqual
  (abstract-demo-guarded-revise
    PENGUIN-BIRD-EVIDENCE BIRD-BIRD-EVIDENCE)
  NONE)
!(assertEqual
  (abstract-demo-guarded-revise
    PENGUIN-BIRD-EVIDENCE DISTINCT-EVIDENCE)
  SOME)
!(assertEqual
  (abstract-demo-semantic-layer-canary)
  True)
