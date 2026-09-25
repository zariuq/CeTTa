; Import and interpretation of read records.  The rewrites below match the
; records' formulas and the names and, if are host builtins; import adds the
; records as data, in order and once per occurrence, and only the explicit
; evaluation in the interpreting space gives them a meaning.

(= (p $x) BOOM)
(= (lives $x) True)

(= (import-test:text)
   "fof(a,axiom,p(X)). fof(a,axiom,p(X)). cnf(and,axiom,~lives(X) | if(X)).")

(= (import-test:run $policy $space $interpreter)
   (let $records
        (tptp:import-text $space (tptp:open-reader strict) $policy (import-test:text))
     (let $stored (collapse (match $space $record $record))
       (let $meaning (tptp:evaluate $interpreter $records)
         (ImportV1 $policy (quote $stored) (quote $meaning))))))
