; Whole packed-artifact acceptance. Domains supply a deterministic observation
; decoder, not a caller-claimed certificate. Every proposal is compared with
; the original task; every byte required by its decoder belongs to the cost.
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

(= (ic-packed:check $model $original $current $candidate)
   (if (and (binary-tree:bytes? $original)
            (and (binary-tree:bytes? $current) (binary-tree:bytes? $candidate)))
     (case (ic-packed:observations $model $original $current $candidate)
       (((IC:Observations (IC:Some $source) (IC:Some $before-source) (IC:Some $after-source))
         (if (and (ic-packed:accepts? $model $source $before-source)
                  (ic-packed:accepts? $model $source $after-source))
           (let* (($before (index-atom (tensor:shape $current) 0))
                  ($after (index-atom (tensor:shape $candidate) 0)))
             (if (< $after $before) (IC:PackedAccepted $candidate $before $after)
               (IC:PackedRejected NotSmaller)))
           (IC:PackedRejected Reconstruction)))
        ($other (IC:PackedRejected InvalidArtifact))))
     (IC:PackedRejected InvalidArtifact)))
