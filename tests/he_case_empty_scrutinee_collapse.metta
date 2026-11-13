; Under collapse, case switches on Empty as it does at top level: with no
; results, the first arm whose pattern matches Empty answers, a variable
; pattern included, in the arms' order; an Empty value meets the Empty arm.
!(collapse (case (empty) (($x z))))
!(collapse (case (superpose ()) ((Empty none) ($x (some $x)))))
!(collapse (case (empty) (($x first) (Empty second))))
!(collapse (case Empty ((Empty none) ($x (some $x)))))
!(let $answers (collapse (case (superpose (a b)) ((Empty none) ($x (some $x))))) (sort-atom $answers))
!(collapse (case (superpose ()) ((foo nofoo))))
(= (collapsed-default) (collapse (case (empty) (($x inside)))))
!(collapsed-default)
