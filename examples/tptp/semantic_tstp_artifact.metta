; Persist and recover a nonempty admitted first-order TSTP derivation carrier.
; Carrier admission preserves semantic data but is not proof verification:
; parent, status, freshness, and calculus obligations remain separate.

!(import! &self lib_tptp)
!(import! &self fs)

(= (example:source-digest)
   (tptp-fo:source-digest "semantic-tstp-artifact-v1"))

(= (example:formula-name $name)
   (tptp-fo:formula-name-atomic
     (tptp-fo:symbol-name (tptp-fo:symbol-lower) $name)))

(= (example:formula $predicate)
   (tptp-fo:formula-atom
     (tptp-fo:atom-predicate
       (tptp-fo:symbol-name (tptp-fo:symbol-lower) $predicate)
       (tptp-fo:terms-nil))))

(= (example:occurrence $index)
   (tptp-fo:occurrence (example:source-digest) $index))

(= (example:role $role)
   (tptp-fo:role $role (tptp-fo:annotation-none)))

(= (example:status-info $status)
   (tstp:info-items-cons
     (tstp:info-status (tstp:status-value $status))
     (tstp:info-items-nil)))

(= (example:named-parent $name)
   (tstp:parent
     (tstp:source-name (example:formula-name $name))
     (tstp:parent-details-none)))

(= (example:leaf-source $name)
   (tstp:source-some
     (tstp:source-file
       (tstp:file-name "semantic_tstp_artifact.p")
       (tptp-fo:formula-name-some (example:formula-name $name)))))

(= (example:input-node $index $name $predicate)
   (tstp:derivation-node
     (example:occurrence $index)
     (tptp-fo:dialect-cnf)
     (example:formula-name $name)
     (example:role "axiom")
     (example:formula $predicate)
     (example:leaf-source $name)
     (tstp:info-items-nil)
     (tptp-fo:source-span-unknown)))

(= (example:derived-source)
   (tstp:source-some
     (tstp:source-inference
       (tstp:rule-name "demo-rule")
       (example:status-info "thm")
       (tstp:parents-cons
         (example:named-parent "input_0")
         (tstp:parents-cons
           (example:named-parent "input_1")
           (tstp:parents-nil))))))

(= (example:derived-node)
   (tstp:derivation-node
     (example:occurrence 2)
     (tptp-fo:dialect-cnf)
     (example:formula-name "derived_2")
     (example:role "plain")
     (example:formula "conclusion")
     (example:derived-source)
     (example:status-info "thm")
     (tptp-fo:source-span-unknown)))

(= (example:derivation)
   (tstp:derivation
     (example:source-digest)
     (tstp:derivation-nodes-cons
       (example:input-node 0 "input_0" "premise_0")
       (tstp:derivation-nodes-cons
         (example:input-node 1 "input_1" "premise_1")
         (tstp:derivation-nodes-cons
           (example:derived-node)
           (tstp:derivation-nodes-nil))))))

(= (example:third-node-source
      (TPTP:StageValue first-order-derivation
        (tstp:derivation $_source
          (tstp:derivation-nodes-cons $_node-0
            (tstp:derivation-nodes-cons $_node-1
              (tstp:derivation-nodes-cons
                (tstp:derivation-node
                  $_occurrence $_dialect $_name $_role $_formula
                  $source $_useful-info $_span)
                $_rest))))))
   $source)

(= (example:info-status
      (tstp:info-items-cons
        (tstp:info-status (tstp:status-value $status)) $_rest))
   $status)

(= (example:parents-count (tstp:parents-nil)) 0)

(= (example:parents-count
      (tstp:parents-cons $_parent $rest))
   (+ 1 (example:parents-count $rest)))

(= (example:source-summary
      (tstp:source-some
        (tstp:source-inference
          (tstp:rule-name $rule) $info $parents)))
   (TSTP:SemanticSource
     $rule (example:info-status $info) (example:parents-count $parents)))

(= (example:derivation-summary $stage-value)
   (example:source-summary (example:third-node-source $stage-value)))

(= (example:stage-rejected
      $stage (TPTP:StageRejected $stage $_reason $_message))
   True)

(= (example:roundtrip-derivation $path $stage-value)
   (case (tptp:to-metta-text $stage-value)
     (((TPTP:MettaText $stage $wire-sha256 $text)
        (let $write-result (fs:write $path $text)
          (let $read-text (fs:read-text $path)
            (let $parsed (tptp:from-metta-text $stage $read-text)
              (let $readmitted (tptp:re-admit-stage-value $parsed)
                (let $reserialized (tptp:to-metta-text $parsed)
                  (case
                    ($write-result
                     (== $text $read-text)
                     $reserialized
                     (== (repr $parsed) (repr $readmitted)))
                    (((() True
                       (TPTP:MettaText
                         $stage $wire-sha256 $text)
                       True)
                       (TPTP:ArtifactRoundTrip
                         $stage $wire-sha256 $text
                         $parsed $readmitted))
                     ($observed
                       (TPTP:ArtifactRoundTripFailed $observed))))))))))
      ($failure (TPTP:ArtifactRoundTripFailed $failure)))))

(= (example:semantic-tstp-artifact)
   (let $admitted
        (tptp:admit-first-order-derivation (example:derivation))
     (let $roundtrip
          (example:roundtrip-derivation
            "runtime/tptp_semantic_derivation_artifact.metta" $admitted)
       (case $roundtrip
         (((TPTP:ArtifactRoundTrip
              first-order-derivation $wire-sha256 $text
              $parsed $readmitted)
            (case
              ((tptp:first-order-derivation-node-count
                 (example:derivation))
               (case $parsed
                 (((TPTP:StageValue first-order-derivation $derivation)
                    (tptp:first-order-derivation-node-count $derivation))
                  ($_ -1)))
               (case $readmitted
                 (((TPTP:StageValue first-order-derivation $derivation)
                    (tptp:first-order-derivation-node-count $derivation))
                  ($_ -1)))
               (example:derivation-summary $admitted)
               (example:derivation-summary $parsed)
               (example:derivation-summary $readmitted)
               (example:stage-rejected first-order-derivation
                 (tptp:admit-first-order-derivation
                   (example:formula "not-a-derivation")))
               (example:stage-rejected first-order-derivation
                 (tptp:re-admit-stage-value
                   (TPTP:StageValue first-order-derivation malformed)))
               (tptp:from-metta-text wrong-stage $text)
               (tptp:from-metta-text
                 first-order-derivation "(malformed")
               (tptp:from-metta-text first-order-derivation
                 (repr
                   (TPTP:SemanticArtifactV1
                     first-order-derivation
                     "0000000000000000000000000000000000000000000000000000000000000000"
                     $parsed)))
               (tptp:from-metta-text first-order-derivation
                 (repr
                   (TPTP:SemanticArtifactV1
                     first-order-derivation $wire-sha256
                     (TPTP:StageValue clause-data malformed))))
               (tptp:from-metta-text first-order-derivation
                 (repr $parsed))
               (tptp:to-metta-text not-a-stage-value))
              (((3 3 3
                 (TSTP:SemanticSource "demo-rule" "thm" 2)
                 (TSTP:SemanticSource "demo-rule" "thm" 2)
                 (TSTP:SemanticSource "demo-rule" "thm" 2)
                 True True
                 (TPTP:MettaTextRejected
                   StageMismatch wrong-stage first-order-derivation)
                 (TPTP:MettaTextRejected
                   ParseFailed first-order-derivation)
                 (TPTP:MettaTextRejected
                   LanguageRevisionMismatch first-order-derivation
                   "0000000000000000000000000000000000000000000000000000000000000000"
                   $wire-sha256)
                 (TPTP:MettaTextRejected
                   EnvelopeStageMismatch
                   first-order-derivation clause-data)
                 (TPTP:MettaTextRejected
                   NotSemanticArtifact
                   first-order-derivation $parsed)
                 (TPTP:MettaTextRejected
                   NotStageValue not-a-stage-value))
                (TPTPSemanticTSTPArtifactV1Summary 18 18 0))
               ($observed
                 (TPTPSemanticTSTPArtifactV1Failed $observed)))))
          ($failure (TPTPSemanticTSTPArtifactV1Failed $failure)))))))

!(println! (example:semantic-tstp-artifact))
