; maplist over several lists and foldl over several lists are SWI's
; maplist/4-5 and foldl/5-7: the callable applied position by position, lists
; of different lengths have no answer.  append of a list of lists is SWI's
; append/2: a type error for a value that is not a list, no answer when an
; element is not a list.
(= (add3 $a $b $c) (+ $a (+ $b $c)))
(= (both $a $b) (superpose (($a $b) ($b $a))))
!(maplist + (1 2) (10 20))
!(maplist add3 (1 2) (10 20) (100 200))
!(maplist + (1 2 3) (10 20))
!(maplist + (1 2) ())
!(maplist (|-> ($x $y) ($y $x)) (a b) (1 2))
!(maplist both (1 2) (a b))
!(catch (maplist + 5 (1)))
!(foldl add3 (1 2) (10 20) 0)
!(foldl + (1 2) 0)
!(append ((1 2) (3) () (4 5)))
!(append (() ()))
!(append ())
!(append ((a) b))
!(append ((1) (quote (2 3))))
!(catch (append 5))
!(catch (append a))
!(let $l (sread "((1) (2))") (append $l))
