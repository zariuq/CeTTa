;; A generated callable crosses into Prolog as the atom lambda_<identity>,
;; and the runtime finds its code again by that identity. Crossing a fresh
;; closure therefore births no permanent symbol for its name. Before, each
;; crossing interned its name: a function added at run time makes a new
;; closure on every call, so this loop added one owned symbol per iteration.
!(add-atom &self (= (make-adder) (|-> ($x) (+ $x 1))))
(= (owned-count) (match (runtime-stats!) (runtime-counter symbol-owned-count $n) $n))
(= (cross-loop $n)
   (if (== $n 0) done
       (let* (($f (make-adder))
              ($ok (translatePredicate (atom $f))))
         (cross-loop (- $n 1)))))
;; The first crossings name the bridge's own vocabulary once.
!(let* (($_ (owned-count)) ($_ (cross-loop 4))) settled)
!(let* (($c (owned-count)) ($_ (cross-loop 2000)))
   (crossings-birth-no-names (< (- (owned-count) $c) 64)))
;; The crossing atom, and its rendered name, still name the same code.
!(let $f (make-adder) (let $_ (translatePredicate (= $g $f)) ($g 41)))
!(let $f (make-adder)
   (let $_ (translatePredicate (atom $f))
     (let $_ (translatePredicate (atom_to_term (repra $f) $g $bindings))
       (== $f $g))))
