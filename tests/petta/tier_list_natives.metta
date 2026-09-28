; PeTTa's append and union-atom, length, exclude-item, list_to_set,
; unique-atom, alpha-unique-atom, car-atom, cdr-atom, decons-atom and decons
; inside relations: over closed lists the tier computes them; a list with
; variables, a partial list, a cons-built list and a non-list answer as
; SWI-PeTTa's relations do.

(= (cat $a $b) (append $a $b))
(= (len $l) (length $l))
(= (without $l $x) (exclude-item $x $l))
(= (uniq $l) (list_to_set $l))

!(cat (1 2) (3 4))
!(cat () (a))
!(cat ((x y) z) ())
!(let $r (cat (1 $v) (2)) (let $v 7 $r))
!(collapse (cat foo (1)))
!(collapse (cat (1) foo))
!(let $q (cons 1 (cons 2 ())) (cat $q (3)))

!(len (a b c))
!(len ())
!(let $q (cons a (cons b ())) (len $q))

!(without (1 2 1 3) 1)
!(without ((a b) c (a b)) (a b))
!(without () x)

!(uniq (1 2 1 3 2))
!(uniq ((a) (b) (a)))
!(uniq ())
!(uniq (1 1.0 1))

; Nested in a larger computation.
(= (stamps $a $b) (msort (cat $a $b)))
!(stamps (3 1) (2 1))
!(len (uniq (cat (1 2 3) (2 3 4))))

; car-atom and cdr-atom: total, () for anything but a non-empty list.
(= (head-of $l) (car-atom $l))
(= (tail-of $l) (cdr-atom $l))
!(head-of (a b c))
!(tail-of (a b c))
!(head-of ())
!(tail-of ())
!(head-of foo)
!(tail-of (x))

; Elements that are variables are carried as they stand and compared by
; identity; binding them afterwards shows they kept their identity.
!(let $r (tail-of ($u $v $w)) (let ($v $w) (1 2) $r))
!(let $r (cat ($u) ($u $v)) (let ($u $v) (5 6) $r))
!(let $r (without ($u a $u) a) (let $u 3 $r))
!(let $r (uniq ($u $v $u)) (let ($u $v) (1 2) $r))
!(len ($u $v))
!(let $r (head-of (($u b) c)) (let $u 9 $r))

; union-atom is append/3; unique-atom is list_to_set, and () for a non-list;
; alpha-unique-atom keeps the first element of each class of alpha-equivalent
; elements, and () for a non-list.
(= (join $a $b) (union-atom $a $b))
(= (dedup $l) (unique-atom $l))
(= (alpha-dedup $l) (alpha-unique-atom $l))
!(join (1 2) (3))
!(let $r (join ($u) ($v $u)) (let ($u $v) (1 2) $r))
!(dedup (b a b c a))
!(dedup foo)
!(let $r (alpha-dedup ((f $x) (f $y) (g $z) (f a))) (let ($x $y $z) (1 2 3) $r))
!(let $r (alpha-dedup ((p $x $x) (p $x $y) (p $y $y))) (let ($x $y) (1 2) $r))
!(alpha-dedup foo)

; decons-atom and decons: a non-empty list is its first element and its
; rest; () and a non-list have no answer.
(= (split $l) (decons-atom $l))
(= (split2 $l) (decons $l))
!(split (a b c))
!(split (a))
!(collapse (split ()))
!(collapse (split foo))
!(split2 ((a b) c))
!(let ($h $t) (split (1 2 3)) (t $t h $h))

; Over computed arguments: the arguments' computations, collections
; included, run first, and the native applies to their values.
(= (gen) (superpose (c a b a)))
(= (sorted-gen) (msort (collapse (gen))))
!(msort (dedup (collapse (gen))))
!(msort (uniq (collapse (gen))))
!(msort (join (collapse (gen)) (collapse (gen))))
!(split (sorted-gen))
!(head-of (sorted-gen))
!(msort (without (collapse (gen)) a))
!(length (alpha-dedup (collapse (superpose ((f $x) (g $y) (f $z))))))

; unique and alpha-unique inside a relation.
(= (uniq-gen) (unique (gen)))
(= (pgen) (superpose ((f $x) (g $y) (f $z))))
(= (alpha-gen) (alpha-unique (pgen)))
!(msort (collapse (uniq-gen)))
!(length (collapse (alpha-gen)))
