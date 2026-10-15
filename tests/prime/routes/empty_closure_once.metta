; A closed application with private closure/Need carriers can finish empty.
; It is complete: retrying to erase bindings must not replay its predicates.
(= (empty-closure:even $x) (let $_ (println! (EVEN $x)) (== (% $x 2) 0)))
(= (empty-closure:small $x) (let $_ (println! (SMALL $x)) (< $x 5)))
(= (empty-closure:and $p $q) (if $p $q False))
(= (empty-closure:not $p) (if $p False True))
!(let $f (lam x (empty-closure:and (empty-closure:even x)
                     (empty-closure:not (empty-closure:small x))))
  (collapse (let $x (superpose (0 1 6 7 8 9))
    (if ($f $x) $x (empty)))))
