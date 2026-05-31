; ============================================================================
; INTENSIONAL WEIGHTED INHERITANCE (§14 Concrete) for CeTTa
; ============================================================================
;
; Mirror of the PeTTa companion layer:
;   - direct property lookup is AUDITED instead of collapsed immediately to a
;     single winner,
;   - packet aggregation uses explicit scratch spaces plus foldl-atom, not
;     recursive car/cdr tuple walking,
;   - packet stamps use a deduplicated union helper instead of shadowing the
;     core StampConcat.
; ============================================================================

!(import! &self lib_pln_query_audit)
!(import! &self lib_overlap_corrected_revision)

!(bind! &intensional_packets (new-space))
!(bind! &intensional_meta (new-space))

(= (Intensional.ClearSpace $space)
   (collapse
     (match $space $atom
            (remove-atom $space $atom))))

(= (Intensional.PacketStampUnion $S1 $S2)
   (Unique (StampConcat $S1 $S2) ()))

(= (Intensional.SourceConceptProps $kb $concept)
   (collapse (match $kb (ConceptProp $concept $P $w) ($P $w))))

(= (PropertyBeliefAudit $kb $entity $prop $maxsteps)
   (collapse
     (match $kb
            (Sentence ((Inheritance $entity (IntSet $prop)) $tv) $stamp)
            ($tv $stamp))))

(= (PropertyBeliefAudit $kb $entity $prop)
   (PropertyBeliefAudit $kb $entity $prop 0))

(= (PropertyBeliefTopTiesFromAudit $audit)
   (PLN.QueryTopTiesFromAudit $audit))

(= (PropertyBeliefTopTies $kb $entity $prop $maxsteps)
   (PropertyBeliefTopTiesFromAudit
     (PropertyBeliefAudit $kb $entity $prop $maxsteps)))

(= (PropertyBeliefTopTies $kb $entity $prop)
   (PropertyBeliefTopTies $kb $entity $prop 0))

(= (ReweighSTV (stv $s $c) $weight)
   (let* (($w (Truth_c2w $c))
          ($scaled_w (* $w $weight))
          ($scaled_c (Truth_w2c $scaled_w)))
         (stv $s (min 1.0 $scaled_c))))

(= (IntensionalPacket ($tv $stamp) $weight)
   ((ReweighSTV $tv $weight) $stamp))

(= (Intensional.PacketSnapshotFrom $packetSpace)
   (Unique
     (collapse
       (match $packetSpace
              (packet $prop $tv $stamp)
              ($prop $tv $stamp)))
     ()))

(= (Intensional.PacketSnapshot)
   (Intensional.PacketSnapshotFrom &intensional_packets))

(= (Intensional.CoveredSnapshotFrom $metaSpace)
   (Unique
     (collapse
       (match $metaSpace (covered $prop) $prop))
     ()))

(= (Intensional.CoveredSnapshot)
   (Intensional.CoveredSnapshotFrom &intensional_meta))

(= (Intensional.MissingSnapshotFrom $metaSpace)
   (Unique
     (collapse
       (match $metaSpace (missing $prop) $prop))
     ()))

(= (Intensional.MissingSnapshot)
   (Intensional.MissingSnapshotFrom &intensional_meta))

(= (Intensional.CollectPropertyPacketsInto $packetSpace $metaSpace $kb $entity $maxsteps ($P $w))
   (let $audit (eval (PropertyBeliefAudit $kb $entity $P $maxsteps))
        (if (== $audit ())
            (let $_missing
                 (add-atom $metaSpace (missing $P))
                 True)
            (let* (($_covered
                     (add-atom $metaSpace (covered $P)))
                    ($_packets
                     (collapse
                       (let $belief (superpose $audit)
                            (case $belief
                                  ((($tv $stamp)
                                    (let $weighted (eval (ReweighSTV $tv $w))
                                         (add-atom $packetSpace
                                                   (packet $P $weighted $stamp))))))))))
                  True))))

(= (Intensional.CollectPropertyPackets $kb $entity $maxsteps ($P $w))
   (Intensional.CollectPropertyPacketsInto
     &intensional_packets &intensional_meta $kb $entity $maxsteps ($P $w)))

(= (Intensional.LoadConceptPacketsInto $packetSpace $metaSpace $kb $entity $concept $maxsteps)
   (let* (($props (eval (Intensional.SourceConceptProps $kb $concept)))
          ($_clearPackets (Intensional.ClearSpace $packetSpace))
          ($_clearMeta (Intensional.ClearSpace $metaSpace))
          ($_collect
            (foldl-atom $props True $acc $pair
              (eval (Intensional.CollectPropertyPacketsInto
                      $packetSpace $metaSpace $kb $entity $maxsteps $pair)))))
         True))

(= (Intensional.LoadConceptPackets $kb $entity $concept $maxsteps)
   (Intensional.LoadConceptPacketsInto
     &intensional_packets &intensional_meta $kb $entity $concept $maxsteps))

(= (Intensional.MergePacketStep ($accTv $accStamp) ($prop $tv $stamp))
   ((Truth_OverlapCorrectedRevision $accTv $tv $accStamp $stamp)
    (Intensional.PacketStampUnion $accStamp $stamp)))

(= (Intensional.ReducePackets ())
   ((stv 0.0 0.0) ()))

(= (Intensional.ReducePackets $packets)
   (foldl-atom $packets ((stv 0.0 0.0) ()) $acc $item
     (eval (Intensional.MergePacketStep $acc $item))))

(= (Intensional.ReducePacketsFrom $packetSpace)
   (Intensional.ReducePackets (Intensional.PacketSnapshotFrom $packetSpace)))

(= (PacketTV ($tv $stamp)) $tv)
(= (PacketStamp ($tv $stamp)) $stamp)

(= (ResultTV (intensional-result $tv $stamp (covered $covered) (missing $missing) (blocked $blocked)))
   $tv)
(= (ResultStamp (intensional-result $tv $stamp (covered $covered) (missing $missing) (blocked $blocked)))
   $stamp)
(= (ResultCovered (intensional-result $tv $stamp (covered $covered) (missing $missing) (blocked $blocked)))
   $covered)
(= (ResultMissing (intensional-result $tv $stamp (covered $covered) (missing $missing) (blocked $blocked)))
   $missing)
(= (ResultBlocked (intensional-result $tv $stamp (covered $covered) (missing $missing) (blocked $blocked)))
   $blocked)

(= (IntensionalMemberSTVInSpaces $packetSpace $metaSpace $kb $entity $concept $maxsteps)
   (let* (($_load (Intensional.LoadConceptPacketsInto $packetSpace $metaSpace $kb $entity $concept $maxsteps))
          ($folded (eval (Intensional.ReducePacketsFrom $packetSpace))))
         (eval (PacketTV $folded))))

(= (IntensionalMemberSTVInSpaces $packetSpace $metaSpace $kb $entity $concept)
   (IntensionalMemberSTVInSpaces $packetSpace $metaSpace $kb $entity $concept 0))

(= (IntensionalMemberSTV $kb $entity $concept $maxsteps)
   (IntensionalMemberSTVInSpaces
     &intensional_packets &intensional_meta $kb $entity $concept $maxsteps))

(= (IntensionalMemberSTV $kb $entity $concept)
   (IntensionalMemberSTV $kb $entity $concept 0))

(= (IntensionalMemberAuditInSpaces $packetSpace $metaSpace $kb $entity $concept $maxsteps)
   (let* (($_load (Intensional.LoadConceptPacketsInto $packetSpace $metaSpace $kb $entity $concept $maxsteps))
          ($folded (eval (Intensional.ReducePacketsFrom $packetSpace))))
         (intensional-result
            (eval (PacketTV $folded))
            (eval (PacketStamp $folded))
            (covered (eval (Intensional.CoveredSnapshotFrom $metaSpace)))
            (missing (eval (Intensional.MissingSnapshotFrom $metaSpace)))
            (blocked ()))))

(= (IntensionalMemberAuditInSpaces $packetSpace $metaSpace $kb $entity $concept)
   (IntensionalMemberAuditInSpaces $packetSpace $metaSpace $kb $entity $concept 0))

(= (IntensionalMemberAudit $kb $entity $concept $maxsteps)
   (IntensionalMemberAuditInSpaces
     &intensional_packets &intensional_meta $kb $entity $concept $maxsteps))

(= (IntensionalMemberAudit $kb $entity $concept)
   (IntensionalMemberAudit $kb $entity $concept 0))
