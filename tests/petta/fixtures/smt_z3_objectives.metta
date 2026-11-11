; Reading the smt library's optimization answers and Z3's objectives, for the
; seeded optimization tests: a verdict, (optimum V), (infimum V), (supremum V),
; unbounded or unsat; Z3's values as numbers, epsilon terms as the value they
; approach, oo as unbounded.
(= (verdict $r)
   (if (== (get-metatype $r) Symbol) $r
     (let $h (car-atom $r)
       (if (== $h unsat) unsat
         (if (== $h unbounded) unbounded
           ($h (index-atom $r 1)))))))

; an exact value as a number: n, or (div P Q)
(= (as-number $v) (if (== (get-metatype $v) Expression) (/ (index-atom $v 1) (index-atom $v 2)) $v))

; Z3's objective value: a number, (- N), (/ A B), with + epsilon or
; - epsilon when only approached, oo or (* (- 1) oo) when unbounded
(= (z3-num $x)
   (if (== (get-metatype $x) Expression)
       (let $h (car-atom $x)
         (if (== $h -) (- 0 (z3-num (index-atom $x 1)))
           (if (== $h /) (/ (z3-num (index-atom $x 1)) (z3-num (index-atom $x 2)))
             (if (== $h *) (* (z3-num (index-atom $x 1)) (z3-num (index-atom $x 2)))
               nan))))
       $x))
(= (has-epsilon $x)
   (if (== $x epsilon) True
     (if (== (get-metatype $x) Expression)
         (if (== $x ()) False (if (has-epsilon (car-atom $x)) True (has-epsilon (cdr-atom $x))))
         False)))
(= (has-oo $x)
   (if (== $x oo) True
     (if (== (get-metatype $x) Expression)
         (if (== $x ()) False (if (has-oo (car-atom $x)) True (has-oo (cdr-atom $x))))
         False)))
; the value an epsilon term approaches: epsilon or (* C epsilon) alone is 0,
; (+ A E) is A
(= (z3-base $x)
   (if (== $x epsilon) 0
     (if (== (car-atom $x) +) (z3-num (index-atom $x 1)) 0)))
; a value of Z3's objectives as a verdict: an interval (a lexicographic stage
; no model attains) by its end the objective approaches
(= (z3-value-verdict $value $max)
   (if (has-oo $value) unbounded
     (if (if (== (get-metatype $value) Expression) (== (car-atom $value) interval) False)
         (z3-value-verdict (index-atom $value (if $max 2 1)) $max)
         (if (has-epsilon $value)
             ((if $max supremum infimum) (z3-base $value))
             (optimum (z3-num $value))))))
(= (agree $ours $theirs)
   (if (== (get-metatype $ours) Symbol) (== $ours $theirs)
     (if (== (get-metatype $theirs) Symbol) False
       (if (== (car-atom $ours) (car-atom $theirs))
           (< (abs-diff (as-number (index-atom $ours 1)) (index-atom $theirs 1)) 0.000000001)
           False))))
(= (abs-diff $a $b) (if (> $a $b) (- $a $b) (- $b $a)))

