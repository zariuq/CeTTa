; Extended profiles expose the strict size spelling for the same occurrence-
; bag cardinality algebra exercised by size-atom in the shared fixture.
(cardinality-size-source alpha left)
(cardinality-size-source alpha left)
(cardinality-size-source alpha right)

(= (cardinality-size-items $key)
   (collapse
     (match &self
            (cardinality-size-source $key $value)
            $value)))

(= (cardinality-size-observe $key)
   (let $items (cardinality-size-items $key)
     (counts (size $items) (size $items))))

; A nondeterministic producer has no singleton unfolding certificate.  Both
; alternatives must remain ordinary strict-size results.
(= (cardinality-size-alternative) (left right))
(= (cardinality-size-alternative) (solo))

!(cardinality-size-observe alpha)
!(cardinality-size-observe missing)
!(size (cardinality-size-items alpha))
!(size (cardinality-size-items missing))
!(size (cardinality-size-alternative))
