; Facts whose spine is their head over holes, the holes ground expressions:
; a relation of deep facts that is a flat table of its holes, read by
; joins by its columns.  Masked views of words of three letters, joined on
; the view; a column fixed to an expression; a pattern taking a column
; apart, which the joins over facts holding variables read; a hole holding
; a variable, which no flat read may take; removals.
!(bind! &w (new-space set))
!(bind! &v (new-space set))
!(bind! &e (new-space set))
(= (word $k) (superpose ((a b c) (a b d) (a c c) (b b c) (a b c) (c c c) (a d d) (b d c))))
!(size-atom (collapse (let $x (word 0) (add-atom &w (w $x)))))
!(size-atom (collapse (match &w (w ($c0 $c1 $c2)) (progn (add-atom &v (mv (N $c1 $c2) $c0 ($c0 $c1 $c2))) (add-atom &v (mv ($c0 N $c2) $c1 ($c0 $c1 $c2))) (add-atom &v (mv ($c0 $c1 N) $c2 ($c0 $c1 $c2)))))))
!(size-atom (collapse (match &v (mv $key $b $x) t)))
; neighbours at distance one: the same view, another letter
!(size-atom (collapse (match &v (mv $key $b1 $x) (match &v (mv $key $b2 $y) (if (== $b1 $b2) (empty) (add-atom &e (e $x $y)))))))
!(sort-atom (collapse (match &e (e (a b c) $y) $y)))
!(size-atom (collapse (match &e (e $x $y) t)))
; a column fixed to an expression
!(sort-atom (collapse (match &v (mv (a b N) $b $x) $b)))
; a pattern taking a column apart
!(sort-atom (collapse (match &v (mv ($p b N) $b ($p b $b)) $p)))
; the views of one word, removed
!(size-atom (collapse (match &v (mv $key $b (a d d)) (remove-atom &v (mv $key $b (a d d))))))
!(size-atom (collapse (match &v (mv $key $b $x) t)))
; a hole holding a variable: the relation is no flat table, and reads answer alike
!(add-atom &v (mv (z N z) $q (z $q z)))
!(sort-atom (collapse (match &v (mv (z N z) $b $x) $b)))
!(size-atom (collapse (match &v (mv $key $b1 $x) (match &v (mv $key $b2 $y) (if (== $b1 $b2) (empty) (add-atom &e (e2 $x $y)))))))
!(size-atom (collapse (match &e (e2 $x $y) t)))
; columns headed by numbers: no symbol heads an expression of the spine, so
; with relations from a head's first fact the relation is flat at once, and
; nested patterns read its columns' terms
!(bind! &f (new-space set))
!(add-atom &f (pt (0 a) (1 b)))
!(add-atom &f (pt (0 b) (1 c)))
!(add-atom &f (pt (0 c) (1 a)))
!(add-atom &f (pt (2 a) (1 b)))
!(sort-atom (collapse (match &f (pt (0 $x) (1 $y)) ($x $y))))
!(sort-atom (collapse (match &f (pt $k (1 b)) (match &f (pt $k $v) $v))))
!(sort-atom (collapse (match &f (pt ($n $x) ($m $x)) ($n $m $x))))
; a nested column whose variable a plain column of another leg binds
!(bind! &k (new-space set))
!(add-atom &k (src a))
!(add-atom &k (src c))
!(sort-atom (collapse (match &k (src $y) (match &f (pt (0 $y) $w) ($y $w)))))
!(size-atom (collapse (match &f (pt (0 $x) $v) (add-atom &f (pt (9 $x) $v)))))
!(size-atom (collapse (match &f (pt $k $v) t)))
!(sort-atom (collapse (match &f (pt (9 $x) (1 $y)) ($x $y))))
; a hole holding a variable: no flat read takes the relation, and the fact
; meets the patterns it unifies with
!(add-atom &f (pt (0 $z) (1 $z)))
!(sort-atom (collapse (match &f (pt (0 q) $v) $v)))
!(size-atom (collapse (match &f (pt (0 $x) (1 $y)) t)))
