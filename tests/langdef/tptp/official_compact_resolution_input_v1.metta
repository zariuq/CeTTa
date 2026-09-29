!(import! &self ../../../lib/lib_tptp.metta)

; Resolution input from compact clauses: occurrences, names, roles, literal
; polarity, equality, the kinds of symbols and numbers, variable scope, and
; the named refusals of a formula and of an unexpanded include.

(= (input:clauses $stage)
   (case $stage
     (((TPTP:StageValue resolution-input (fo-resolution:problem $_source $clauses))
        $clauses)
      ($other (NotAProblem $other)))))

(= (input:first (fo-resolution:clauses-cons $clause $_rest)) $clause)
(= (input:second (fo-resolution:clauses-cons $_first (fo-resolution:clauses-cons $clause $_rest))) $clause)

(= (input:occurrence-index (fo-resolution:clause (fo-resolution:occurrence $_source $index) $_n $_r $_l))
   $index)
(= (input:name (fo-resolution:clause $_o $name $_r $_l)) $name)
(= (input:role (fo-resolution:clause $_o $_n $role $_l)) $role)
(= (input:literals (fo-resolution:clause $_o $_n $_r $literals)) $literals)

; The variables of a clause's first literal's first argument and of the
; second clause's: the same name in two inputs is two variables.
(= (input:first-variable
      (fo-resolution:clause $_o $_n $_r
        (fo-resolution:literals-cons
          (fo-resolution:literal-positive
            (fo-resolution:atom-predicate $_p
              (fo-resolution:terms-cons (fo-resolution:term-variable $variable) $_ts)))
          $_ls)))
   $variable)

!(let $text "cnf(c1,axiom,p(X)|~q(X,f(a))). fof(skip,axiom,r). cnf('Named',hypothesis-assumed,p(X)|X != Y|'Q'(42,1/3,-2.5,\"d\",$true,$$s,b)). cnf(7,axiom,a=b)."
   (let $clauses (input:clauses (tptp:read-text-resolution-input $text))
     (println! (ResolutionInputRefusal $clauses))))

!(let $text "cnf(c1,axiom,p(X)|~q(X,f(a))). cnf('Named',hypothesis-assumed,p(X)|X != Y|'Q'(42,1/3,-2.5,\"d\",$true,$$s,b)). cnf(7,axiom,a=b). cnf('cat',axiom,cat)."
   (let $clauses (input:clauses (tptp:read-text-resolution-input $text))
     (let $first (input:first $clauses)
       (let $second (input:second $clauses)
         (println!
           (ResolutionInputV1
             (input:occurrence-index $first)
             (input:occurrence-index $second)
             (input:name $first)
             (input:name $second)
             (input:role $second)
             (input:literals $first)
             (input:literals $second)
             (== (input:first-variable $first) (input:first-variable $second))))))))

!(let $clauses (input:clauses (tptp:read-text-resolution-input "cnf(7,axiom,a=b). cnf('cat',axiom,cat). cnf(c,axiom,'cat')."))
   (println! (ResolutionInputNamesV1 $clauses)))

!(println! (tptp:read-text-resolution-input "include('Axioms/SET001-0.ax')."))

; A reader fault reaches the caller unchanged.
!(println! (tptp:read-text-resolution-input "fof(a,axiom,)."))
