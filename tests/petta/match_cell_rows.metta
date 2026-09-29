; A row with variables meets a query holding a list built at run time as the
; list it spells, at the root of an argument or nested in it.
(= (nodup $s $a) (case (match $s $a $a) (($a ()) (Empty (add-atom $s $a)))))
!(add-atom &open (s (, (I $a soda) (I $a ugly) (I $a man)) 0.0))
!(add-atom &ground (s (, (I u soda) (I u ugly) (I u man)) 0.0))
!(let $c (cons-atom , ((I $x soda) (I $x ugly) (I $x $v))) (collapse (match &ground (s $c 0.0) ground)))
!(let $c (cons-atom , ((I $x soda) (I $x ugly) (I $x $v))) (collapse (match &open (s $c 0.0) open)))
!(let $c (cons-atom , ((I u soda) (I u ugly) (I u man))) (collapse (match &open (s $c 0.0) open-ground-query)))
!(let $c (cons-atom , ((I $x soda) (I $y ugly) (I $z $v))) (collapse (match &open (s $c 0.0) open-unshared)))
!(let $c (cons-atom , ((I $x soda) (I $x ugly) (I $x $v))) (collapse (match &open (s $c 0.0) (s $c 0.0))))
!(let* (($t (cons-atom (I $x ugly) ((I $x $v)))) ($c (cons-atom , (cons-atom (I $x soda) $t)))) (collapse (match &open (s $c 0.0) nested)))
; A query the row cannot meet: the second conjunct differs.
!(let $c (cons-atom , ((I $x soda) (I $x woman) (I $x $v))) (collapse (match &open (s $c 0.0) none)))
; Deduplication by unification: the general pattern meets the stored row.
!(let* (($t (cons-atom (I $x ugly) ((I $x $v)))) ($c (cons-atom , (cons-atom (I $x soda) $t)))) (nodup &open (s $c 0.0)))
!(collapse (get-atoms &open))
