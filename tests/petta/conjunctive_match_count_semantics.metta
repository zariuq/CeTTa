; A ground conjunction leg contributes its exact occurrence multiplicity to
; every continuation under a cardinality observation.  The duplicate guard
; and three tail rows therefore yield six occurrences for alpha; beta is
; eliminated by the absent guard.
(join-seed alpha)
(join-seed beta)
(join-guard alpha)
(join-guard alpha)
(join-tail left)
(join-tail middle)
(join-tail right)

!(length
   (collapse
     (match &self
            (, (join-seed $key)
               (join-guard $key)
               (join-tail $tail))
            ($key $tail))))

; Zero multiplicity prunes the continuation.
!(length
   (collapse
     (match &self
            (, (join-seed $key)
               (missing-guard $key)
               (join-tail $tail))
            ($key $tail))))
