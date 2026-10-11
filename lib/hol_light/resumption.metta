; Event budgets count source inference requests, not evaluator instructions.
; A work value retains the complete receiver result and the unconsumed input.
; Resume only work obtained from this pinned checker and a checked prefix.
; A reconstructed work value is data; its shape does not authorize a theorem.

(: hol-light:work (-> Atom %Undefined%))
(= (hol-light:work $events)
  (if (hol-light:ground $events)
    (if (hol-light:list-valid $events)
      (let $seed (hol-light:standard-receive HOL:Nil)
        (HOL:Work $seed $events 0)) None) None))

(: hol-light:resume (-> Number Atom %Undefined%))
(= (hol-light:resume $budget (HOL:Work $result $events $consumed))
  (if (< $budget 0) None
    (if (== $budget 0) (HOL:Work $result $events $consumed)
      (case $events
        ((HOL:Nil (HOL:Work $result HOL:Nil $consumed))
         ((HOL:Cons $event $rest)
          (let* (($next (case $result
                        (((Some $state)
                          (hol-light:receive-from $state (HOL:Cons $event HOL:Nil)))
                         ($refused None))))
                 ($remaining (- $budget 1))
                 ($count (+ $consumed 1))
                 ($work (HOL:Work $next $rest $count)))
            (hol-light:resume $remaining $work)))
         ($malformed None))))))

(= (hol-light:work-result (HOL:Work $result $events $consumed)) $result)
(= (hol-light:work-rest (HOL:Work $result $events $consumed)) $events)
(= (hol-light:work-consumed (HOL:Work $result $events $consumed)) $consumed)
