; Whole packed-artifact acceptance. Domains supply a deterministic observation
; decoder, not a caller-claimed certificate. Every proposal is compared with
; the original task. Bytes charges every serialized byte. Other explicit cost
; policies must supply their own measure for the admitted domain.
!(import! &self (library lib_binary_tree))

; Domain observation is deterministic. Share only within this operation and
; only for identical byte values; neither approximate observation equality
; nor a retained, caller-claimed observation is a cache key.
(= (ic-packed:observations $model $original $current $candidate)
   (let* (($source (ic-packed:observe $model $original))
          ($before (if (== $current $original) $source (ic-packed:observe $model $current)))
          ($after (if (== $candidate $original) $source
                    (if (== $candidate $current) $before (ic-packed:observe $model $candidate)))))
     (IC:Observations $source $before $after)))

(= (ic-packed:cost Bytes $model $artifact)
   (IC:Some (index-atom (tensor:shape $artifact) 0)))
(= (ic-packed:check $model $original $current $candidate)
   (ic-packed:check Bytes $model $original $current $candidate))
(= (ic-packed:check $policy $model $original $current $candidate)
   (if (not (or (== $policy Bytes) (== $policy Nodes))) (IC:PackedRejected InvalidCost)
    (if (and (binary-tree:bytes? $original)
            (and (binary-tree:bytes? $current) (binary-tree:bytes? $candidate)))
     (case (ic-packed:observations $model $original $current $candidate)
       (((IC:Observations (IC:Some $source) (IC:Some $before-source) (IC:Some $after-source))
         (if (and (ic-packed:accepts? $model $source $before-source)
                  (ic-packed:accepts? $model $source $after-source))
           (case ((ic-packed:cost $policy $model $current) (ic-packed:cost $policy $model $candidate))
             ((((IC:Some $before) (IC:Some $after))
               (if (and (binary:unsigned? $before) (binary:unsigned? $after))
                 (if (< $after $before) (IC:PackedAccepted $candidate $before $after)
                   (IC:PackedRejected NotSmaller)) (IC:PackedRejected InvalidCost)))
              ($invalid-cost (IC:PackedRejected InvalidCost))))
           (IC:PackedRejected Reconstruction)))
        ($other (IC:PackedRejected InvalidArtifact))))
     (IC:PackedRejected InvalidArtifact))))
