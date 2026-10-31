; A Data parameter holds its argument as written.  Comparing the held term
; with == reads it as syntax: nothing inside it runs, whether or not it
; contains variables, and the answer is about the term as written.
(= (exists $d $p) (println! ran-exists))
(: is-nil (-> Atom Bool))
(= (is-nil $t) (== $t ()))
!(is-nil (exists P (lam y (P y))))
!(is-nil (exists P (lam $y (P $y))))
!(is-nil ())

; The same under if, and between two held terms.
(: is-nil-if (-> Atom Bool))
(= (is-nil-if $t) (if (== $t ()) True False))
!(is-nil-if (exists P (lam $y (P $y))))
(: same (-> Atom Atom Bool))
(= (same $a $b) (== $a $b))
!(same (exists P (lam $y (P $y))) (exists P (lam $y (P $y))))
!(same (exists P (lam $y (P $y))) (exists Q (lam $y (Q $y))))

; The held term is returned as written, and runs only when evaluated.
(: hold (-> Atom Atom))
(= (hold $t) $t)
!(hold (exists P (lam $y (P $y))))
!(let $d (hold (exists P (lam $y (P $y)))) (eval $d))

; Substitution may carry a held value inside the returned syntax. Each
; authored-term observer reads the same data without activating its fields.
!(bind! &held-equality-effects (new-space))
(= (held-equality:tick)
   (let $_ (add-atom &held-equality-effects ran) 7))
!(let $term (atom-subst 3 $x (Box $x)) (== $term (Box 3)))
!(let $term (atom-subst 3 $x (Box $x)) (== $term (Box 4)))
!(let $term (atom-subst 3 $x (Box $x)) (=alpha $term (Box 3)))
!(let $term (atom-subst 3 $x (Box $x)) (=alpha $term (Box 4)))
!(let $term (atom-subst 3 $x (Box $x))
   (if-equal $term (Box 3) (Chosen same) (held-equality:tick)))
!(let $term (atom-subst 3 $x (Box $x))
   (if-equal $term (Box 4) (held-equality:tick) (Chosen different)))
!(== (hold (Box (held-equality:tick))) (hold (Box (held-equality:tick))))
!(collapse (get-atoms &held-equality-effects))
!(let $code (hold (held-equality:tick)) (eval $code))
!(collapse (get-atoms &held-equality-effects))
!(let $shared (held-equality:tick) (== (Box $shared) (Box $shared)))
!(collapse (get-atoms &held-equality-effects))
!(let $predicate (atom-subst 3 $x (== (Box $x) (Box 3))) (eval $predicate))
