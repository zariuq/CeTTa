; Heads that read a list by head and tail, repeat a variable, or run a function.
(= (as $whole $part) (let $whole $part $whole))
(= (first-and-all (as $l (cons $h $t))) (pair $h $l))
!(test (first-and-all (1 2 3)) (pair 1 (1 2 3)))

(= (len ()) 0)
(= (len (cons $h $t)) (+ 1 (len $t)))
!(test (len (a b c d)) 4)

(= (twin $x $x) yes)
!(test (collapse (twin 1 1)) (yes))
!(test (collapse (twin 1 2)) ())

(= (kind (leaf $v)) leaf)
(= (kind (node $l $r)) node)
(= (kind $other) something)
!(test (collapse (kind (node (leaf 1) (leaf 2)))) (node something))

(= (depth (leaf $v)) 0)
(= (depth (node $l $r)) (+ 1 (max (depth $l) (depth $r))))
!(test (depth (node (node (leaf 1) (leaf 2)) (leaf 3))) 2)
