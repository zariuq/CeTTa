; Closed eager calls reuse a revision-pinned prepared program within one
; top-level HE evaluation.  A semantic mutation of the cached relation must
; invalidate that program before the next call.
(= (prepared-he-cache-step $x) (+ $x 1))

(= (prepared-he-cache-sequence)
   (let $a (prepared-he-cache-step 1)
     (let $b (prepared-he-cache-step 2)
       (let $_
            (add-atom &self
                      (= (prepared-he-cache-step $y) (+ $y 10)))
            (PreparedCacheResult
              $a $b (prepared-he-cache-step 5))))))

!(prepared-he-cache-sequence)
