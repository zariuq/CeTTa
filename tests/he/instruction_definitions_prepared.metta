; Definitions written in HE's minimal instructions run in the prepared
; machine as HE's instruction machine runs them: where the expansion's syntax
; decides a chain or a unify, it runs at compile time, and a deconstructed
; value binds when the program runs.  A pattern that does not match answers
; the else branch, and malformed arguments answer HE's errors.
(: my-let (-> Atom %Undefined% Atom %Undefined%))
(= (my-let $pattern $atom $template) (unify $atom $pattern $template Empty))
(: my-let* (-> Expression Atom %Undefined%))
(= (my-let* $pairs $template)
  (chain (decons-atom $pairs) $ht
    (unify ($head $tail) $ht
      (unify ($pattern $atom) $head
        (my-let $pattern $atom (my-let* $tail $template))
        (Error (my-let* $pairs $template) "List of (<pattern> <atom>) pairs is expected as a second argument"))
      $template )))
(: my-car (-> Expression %Undefined%))
(= (my-car $atom)
  (chain (decons-atom $atom) $ht (unify ($head $_) $ht
    $head
    (Error (my-car $atom) "car-atom expects a non-empty expression as an argument") )))
(: my-cdr (-> Expression Expression))
(= (my-cdr $atom)
  (chain (decons-atom $atom) $ht (unify ($_ $tail) $ht
    $tail
    (Error (my-cdr $atom) "cdr-atom expects a non-empty expression as an argument") )))
(= (loop $n $acc) (if (== $n 0) $acc (my-let* (($m (- $n 1)) ($a (+ $acc 1))) (loop $m $a))))
(= (deep $x) (my-let* (($a (+ $x 1)) ($b (* $a 2)) ($c (- $b $a)) ($d (+ $c $c))) ($a $b $c $d)))
(= (pat $x) (my-let* (((pair $p $q) (pair $x (+ $x 1))) ($s (+ $p $q))) $s))
(= (nomatch $x) (my-let* (((pair $p $q) $x)) (+ $p $q)))
(= (bad $x) (my-let* (($a $x) oops) $a))
(= (empty-pairs $x) (my-let* () (+ $x 1)))
(= (heads $l) (my-car $l))
(= (tails $l) (my-cdr $l))
(= (sum-list $l) (if (== $l ()) 0 (+ (my-car $l) (sum-list (my-cdr $l)))))
!(loop 300 0)
!(deep 5)
!(pat 3)
!(collapse (nomatch (pair 1 2)))
!(collapse (nomatch 7))
!(collapse (bad 1))
!(empty-pairs 41)
!(heads (7 8 9))
!(tails (7 8 9))
!(collapse (heads ()))
!(collapse (tails ()))
!(sum-list (1 2 3 4))
!(heads ((+ 1 2) 4))
!(my-car ($v 1))
