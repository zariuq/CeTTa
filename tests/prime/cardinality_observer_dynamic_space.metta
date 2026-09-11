; Prime persists syntax without forcing it.  The loader therefore makes each
; coordinate a demanded local value before crossing the storage boundary.
!(bind! &cardinality-dynamic-space (new-space))

(= (cardinality-load $remaining)
   (let $key (+ $remaining 0)
     (if (== $key 0)
         done
         (let $bucket (% $key 10)
           (if (== $bucket $bucket)
               (let $_
                    (add-atom
                      &cardinality-dynamic-space
                      (cardinality-row $key $bucket))
                 (cardinality-load (- $key 1)))
               impossible)))))

(= (cardinality-all)
   (collapse
     (match &cardinality-dynamic-space
            (cardinality-row $key $bucket)
            (cardinality-row $key $bucket))))
(= (cardinality-first $key)
   (collapse
     (match &cardinality-dynamic-space
            (cardinality-row $key $bucket)
            (cardinality-row $key $bucket))))
(= (cardinality-second $bucket)
   (collapse
     (match &cardinality-dynamic-space
            (cardinality-row $key $bucket)
            (cardinality-row $key $bucket))))
(= (cardinality-both $key $bucket)
   (collapse
     (match &cardinality-dynamic-space
            (cardinality-row $key $bucket)
            (cardinality-row $key $bucket))))

!(let $_ (cardinality-load 1000)
   (let* (($all (cardinality-all))
          ($first (cardinality-first 7))
          ($second (cardinality-second 3))
          ($both (cardinality-both 42 2)))
     (counts (size-atom $all)
             (size-atom $first)
             (size-atom $second)
             (size-atom $both))))
