; Preserve a real official all-family TSTP semantic derivation as a reusable,
; revision-keyed MeTTa artifact.  Carrier admission is deliberately weaker
; than derivation verification: the duplicate-node control below is admitted
; as well-sorted data, but is not called a verified proof.

!(import! &self lib_tptp)
!(import! &self fs)

(= (example:read-one $path)
   (let $text (fs:read-text $path)
     (case (collapse (parse $text))
       ((((Error $_source $_message))
          (TPTP:OfficialDerivationFixtureRejected ParseFailed))
        (($value) $value)
        ($observed
          (TPTP:OfficialDerivationFixtureRejected
            ParseMultiplicity $observed))))))

(= (example:nodes-count
      (tptp-semantic:derivation-nodes-nil))
   0)

(= (example:nodes-count
      (tptp-semantic:derivation-nodes-cons $_first $rest))
   (+ 1 (example:nodes-count $rest)))

(= (example:derivation-node-count
      (TPTP:StageValue official-derivation
        (tptp-semantic:derivation $_source $nodes)))
   (example:nodes-count $nodes))

(= (example:duplicate-first-node
      (tptp-semantic:derivation $source
        (tptp-semantic:derivation-nodes-cons $first $_rest)))
   (tptp-semantic:derivation $source
     (tptp-semantic:derivation-nodes-cons $first
       (tptp-semantic:derivation-nodes-cons $first
         (tptp-semantic:derivation-nodes-nil)))))

(= (example:wrong-node-derivation)
   (tptp-semantic:derivation
     (tptp-semantic:source-digest "fixture")
     (tptp-semantic:derivation-nodes-cons
       (tptp-semantic:source-digest "not-an-annotated-input")
       (tptp-semantic:derivation-nodes-nil))))

(= (example:stage-rejected
      $stage (TPTP:StageRejected $stage $_reason $_message))
   True)

(= (example:stage-value
      $stage (TPTP:StageValue $stage $_value))
   True)

(= (example:official-derivation-artifact)
   (let $derivation
        (example:read-one
          "tests/langdef/tptp/official_ground_resolution_refutation_v9200.semantic.metta")
     (let $admitted (tptp:admit-official-derivation $derivation)
       (case (tptp:to-metta-text $admitted)
         (((TPTP:MettaText official-derivation $wire-sha256 $text)
            (let $write-result
                 (fs:write
                   "runtime/tptp_official_derivation_artifact.metta" $text)
              (let $read-text
                   (fs:read-text
                     "runtime/tptp_official_derivation_artifact.metta")
                (let $parsed
                     (tptp:from-metta-text official-derivation $read-text)
                  (let $readmitted (tptp:re-admit-stage-value $parsed)
                    (let $reserialized (tptp:to-metta-text $parsed)
                      (let $duplicate
                           (example:duplicate-first-node $derivation)
                        (let $duplicate-admitted
                             (tptp:admit-official-derivation $duplicate)
                          (case
                            ((example:derivation-node-count $admitted)
                             (example:derivation-node-count $parsed)
                             (example:derivation-node-count $readmitted)
                             $write-result
                             (== $text $read-text)
                             $reserialized
                             (== (repr $parsed) (repr $readmitted))
                             (tptp:from-metta-text wrong-stage $text)
                             (tptp:from-metta-text official-derivation
                               (repr
                                 (TPTP:SemanticArtifactV1
                                   official-derivation
                                   "0000000000000000000000000000000000000000000000000000000000000000"
                                   $parsed)))
                             (tptp:from-metta-text official-derivation
                               (repr
                                 (TPTP:SemanticArtifactV1
                                   official-derivation $wire-sha256
                                   (TPTP:StageValue
                                     first-order-derivation malformed))))
                             (tptp:from-metta-text official-derivation
                               (repr $parsed))
                             (example:stage-rejected official-derivation
                               (tptp:admit-official-derivation
                                 (example:wrong-node-derivation)))
                             (example:stage-value official-derivation
                               $duplicate-admitted)
                             (example:derivation-node-count
                               $duplicate-admitted)
                             (example:stage-rejected first-order-derivation
                               (tptp:admit-first-order-derivation
                                 $derivation)))
                            (((5 5 5 () True
                               (TPTP:MettaText
                                 official-derivation $wire-sha256 $text)
                               True
                               (TPTP:MettaTextRejected
                                 StageMismatch wrong-stage
                                 official-derivation)
                               (TPTP:MettaTextRejected
                                 LanguageRevisionMismatch
                                 official-derivation
                                 "0000000000000000000000000000000000000000000000000000000000000000"
                                 $wire-sha256)
                               (TPTP:MettaTextRejected
                                 EnvelopeStageMismatch
                                 official-derivation
                                 first-order-derivation)
                               (TPTP:MettaTextRejected
                                 NotSemanticArtifact
                                 official-derivation $parsed)
                               True True 2 True)
                              (TPTPOfficialDerivationArtifactV1Summary
                                15 15 0))
                             ($observed
                               (TPTPOfficialDerivationArtifactV1Failed
                                 $observed))))))))))))
          ($failure
            (TPTPOfficialDerivationArtifactV1Failed $failure)))))))

!(println! (example:official-derivation-artifact))
