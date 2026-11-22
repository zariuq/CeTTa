; A clause that is never evaluated changes no answer.  visit-tree fails at
; (kids $p), since &st holds no row, so visit-node and the case inside it are
; never evaluated, and (visit-list (1)) is ().  The unreached step clause
; destructures with a variable-headed let pattern.
(= (kids $p) (once (match &st (row $p $k) $k)))
(= (step (candidate $c $d) (fc $a $b)) (let ($m $n) (f0 ($c $a)) (candidate $m $n)))
(= (visit-node $p) (case (kind $p) ((rule (step no-candidate (fc a b))) ($_ ()))))
(= (visit-tree $p) (let $c (visit-list (kids $p)) (visit-node $p)))
(= (visit-list ()) ())
(= (visit-list (cons $p $rest)) (let $_ (collapse (visit-tree $p)) (visit-list $rest)))
!(println! (literal (visit-list (1))))
!(println! (literal (visit-list (1 2 3))))
