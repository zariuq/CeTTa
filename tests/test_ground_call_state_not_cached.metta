;; A state-writing call has an impure receipt delta (WRITE_STATE/READ_STATE) and
;; must not be memoized: each call must run its effect.  If it were cached, the
;; second call would replay the first result and skip the increment.  (State
;; threads within one world, so the observation is inside a single let*.)
!(let* (
    (() (pragma! search-table-mode ground-call))
    (() (bind! &c (new-state 0)))
    (() (add-atom &self
          (= (gc:tick $n)
             (let $_ (change-state! &c (+ (get-state &c) 1)) (get-state &c)))))
    ($a (gc:tick 0))
    ($b (gc:tick 0))
    (() (assertEqual $a 1))
    (() (assertEqual $b 2))
  ) ())
