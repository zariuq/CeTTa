; A closed ready call may reuse its revision-pinned prepared program within
; one PeTTa machine episode.  The argument values remain dynamic.
(= (prepared-cache-step $x) (+ $x 1))

; The first two calls share one space revision.  An unrelated semantic
; mutation then invalidates that program before the third call; recompilation
; must preserve the new revision rather than executing stale code.
(= (prepared-cache-sequence $x)
   (let $a (prepared-cache-step $x)
     (let $b (prepared-cache-step (+ $x 1))
       (let $_
            (add-atom &self
                      (= (prepared-cache-unrelated $y) $y))
            (prepared-cache-step (+ $a $b))))))

; Transaction working spaces are deliberately outside the cache's lifetime
; contract.  The call remains executable, but its temporary space must not be
; retained as reusable metadata after the transaction ends.
(= (prepared-cache-in-transaction $x)
   (transaction (prepared-cache-step $x)))

!(prepared-cache-sequence 1)
!(prepared-cache-in-transaction 9)
