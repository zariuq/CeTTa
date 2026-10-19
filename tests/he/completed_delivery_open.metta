; A shared value with a variable in it is built and instantiated once per
; distinct node: substituting into it keeps its sharing.
(= (dag $n $leaf) (if (== $n 0) $leaf (let $t (dag (- $n 1) $leaf) (pair $t $t))))
!(let $d (dag 60 $x) ok)
!(let $d (dag 60 $x) (let $x 1 (let $e $d ok)))
!(let $d (dag 60 $x) (let $x 1 (== $d (dag 60 1))))
!(let $d (dag 3 $x) (let $x 1 $d))
!(let $d (dag 2 $x) $d)
