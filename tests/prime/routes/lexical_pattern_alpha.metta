; Alpha comparison respects pattern binders and preserves free-name distinctions.
(scope:profile lexical-fresh)
!(== (lam z (let $x z $x)) (lam w (let $y w $y)))
!(== (lam z (let $x z (Pair $x $a))) (lam w (let $y w (Pair $y $b))))
!(== @(let $x 1 $x) @(let $y 1 $y))
!(== @(let* (($x 1) ($y $x)) (Pair $x $y)) @(let* (($a 1) ($b $a)) (Pair $a $b)))
!(== @(case 1 (($x $x))) @(case 1 (($y $y))))
!(== @(let $x $a $x) @(let $y $b $y))
