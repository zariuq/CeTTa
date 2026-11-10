; The first native occurrence may be beneath an unused let: no earlier direct
; call warms the planner or the native-name registry.
!(let $unused anything (transform &self () x))

; Extended transform remains computation beneath an unused let.  Its source is
; not evaluated as data, and its template cannot run without the take.
!(let $s (new-space)
   (let $empty (transform $s () x)
      (let $count (transform $s () (, (item a) (item b)) $s)
         (let $left (collapse (match $s (item $x) $x))
            (destination $empty $count (sort-atom $left))))))

!(let $s (new-space)
   (let $_ (add-atom $s (job a))
      (let $unused anything
         (let $answer (transform $s (take (job a)) done)
            (let $left (collapse (match $s (job $x) $x))
               (taken $answer $left))))))

!(let $s (new-space)
   (let $_ (add-atom $s (job a))
      (let $unused anything
         (let $answer (transform $s (take (job a))
                         (let $_ (add-atom $s (seen)) done))
            (let $jobs (collapse (match $s (job $x) $x))
               (let $seen (collapse (match $s (seen) yes))
                  (effectful $answer $jobs $seen)))))))

; The first firing removes the lock.  Later candidates must not reuse their
; enabling read; the second item stays.
!(let $s (new-space)
   (let $_ (add-atom $s (item a))
      (let $_ (add-atom $s (item b))
         (let $_ (add-atom $s (lock))
            (let $answers (collapse (transform $s
               (, (read (lock)) (take (item $x)))
               (let $_ (remove-atom $s (lock)) (got $x))))
               (let $left (collapse (match $s (item $x) $x))
                  (lost-keep $answers $left)))))))
