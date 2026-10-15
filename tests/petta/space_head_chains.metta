; Declarations and equations are read through the chain of their head.

(: area (-> Number Number))
(: label (-> Atom Atom))
(: shift (-> Number Number))
(: shift (-> Atom Atom))
!(get-type area)
!(get-type label)
!(collapse (get-type shift))
!(get-type perimeter)

(= (twice $x) (* 2 $x))
(= (twice $x $y) (+ $x $y))
(= (thrice $x) (* 3 $x))
!(twice 4)
!(twice 4 5)
!(thrice 4)
!(halve 4)

!(remove-atom &self (= (twice $x $y) (+ $x $y)))
!(twice 4)

(= (later) found)
!(later)
