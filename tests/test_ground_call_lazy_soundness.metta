;; The memo keys by forcing only syntactically-pure arithmetic arguments, so a
;; divergent argument the callee never demands stays unevaluated: this call
;; terminates with the memo enabled.
!(let* (
    (() (pragma! search-table-mode ground-call))
    (() (add-atom &self (= (gc:const $x $y) $x)))
    (() (add-atom &self (= (gc:loop $n) (gc:loop $n))))
    (() (assertEqual (gc:const 5 (gc:loop 0)) 5))
  ) ())
