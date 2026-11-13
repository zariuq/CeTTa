; A list built cell by cell around an unbound element is held as cells;
; once the element is bound, the cells spell an ordinary list.  A let
; pattern meets the list the cells spell, never the cells' own fields, in
; every compiled tier as in the machine.
(= (rebuild $t)
   (if (== (get-metatype $t) Expression)
       (if (== $t ()) $t (rebuild-each $t))
       $t))
(= (rebuild-each $ts)
   (if (== $ts ()) ()
     (let ($t $more) (decons-atom $ts)
       (let $one (rebuild $t)
         (let $tail (rebuild-each $more) (cons-atom $one $tail))))))
(= (bind $v $value) (let $v $value done))
(= (read-fields $t) (let ($h $l $r) $t (got $h $l $r)))
(= (read-headed $t) (let (eq $l $r) $t (got $l $r)))
(= (read-same $t) (let (eq $l $r) $t (let $l $r solved)))
(= (read-pair $t) (let ($a $b) $t (two $a $b)))
(= (read-four $t) (let ($a $b $c $d) $t (four $a $b $c $d)))
(= (read-other-head $t) (let (neq $l $r) $t (got $l $r)))
(= (tell-all $ts)
   (if (== $ts ()) solved
     (let ($t $more) (decons-atom $ts)
       (let (eq $l $r) $t (let $l $r (tell-all $more))))))
; Positive: the cells are read as the list (eq b b).
!(fields (collapse (let* (($t0 (quote (eq $z b))) ($t (rebuild $t0)) ($d (bind $z b))) (read-fields $t))))
!(headed (collapse (let* (($t0 (quote (eq $z b))) ($t (rebuild $t0)) ($d (bind $z b))) (read-headed $t))))
!(same (collapse (let* (($t0 (quote (eq $z b))) ($t (rebuild $t0)) ($d (bind $z b))) (read-same $t))))
!(deep (collapse (let* (($ts0 (quote ((eq $z b)))) ($ts (rebuild $ts0)) ($d (bind $z b))) (tell-all $ts))))
; Negative: a pattern of another length or head does not meet the list.
!(pair (collapse (let* (($t0 (quote (eq $z b))) ($t (rebuild $t0)) ($d (bind $z b))) (read-pair $t))))
!(four (collapse (let* (($t0 (quote (eq $z b))) ($t (rebuild $t0)) ($d (bind $z b))) (read-four $t))))
!(other-head (collapse (let* (($t0 (quote (eq $z b))) ($t (rebuild $t0)) ($d (bind $z b))) (read-other-head $t))))
!(unequal (collapse (let* (($t0 (quote (eq $z b))) ($t (rebuild $t0)) ($d (bind $z c))) (read-same $t))))
