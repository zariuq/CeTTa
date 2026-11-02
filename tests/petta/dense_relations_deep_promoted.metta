; A deep relation its head takes after counting facts as rows: the facts
; added before it stay rows and those after go to the relation, and a set
; holds each fact once wherever it is held.  Added again, ground or as a
; variant, a fact adds nothing; removed, it goes from where it is held.
!(bind! &s (new-space set))
(= (fill-w $k $d $w)
   (if (>= $w 16) done
       (let* (($a (add-atom &s (g $d $w (C (/ (> p (q $k)) (> $x $x)) (r $k)))))
              ($b (add-atom &s (h $d $w (C (/ (> p (q $k)) s) (r $k))))))
         (fill-w $k $d (+ $w 1)))))
(= (fill-d $k $d)
   (if (>= $d 16) done
       (let $f (fill-w $k $d 0) (fill-d $k (+ $d 1)))))
(= (fill $k $n)
   (if (>= $k $n) done
       (let $f (fill-d $k 0) (fill (+ $k 1) $n))))
!(fill 0 20)
!(size-atom (collapse (match &s (g $d $w $c) t)))
!(size-atom (collapse (match &s (h $d $w $c) t)))
; the first facts, rows, and the last, the relation's, added again
!(fill-w 0 0 0)
!(fill-w 19 15 0)
!(size-atom (collapse (match &s (g $d $w $c) t)))
!(size-atom (collapse (match &s (h $d $w $c) t)))
; as variants
!(add-atom &s (g 0 3 (C (/ (> p (q 0)) (> $u $u)) (r 0))))
!(add-atom &s (g 15 3 (C (/ (> p (q 19)) (> $u $u)) (r 19))))
!(size-atom (collapse (match &s (g $d $w $c) t)))
; facts the head has never held, open and ground
!(add-atom &s (g 0 0 (C (/ (> p (q 99)) (> $u $u)) (r 99))))
!(add-atom &s (h 0 0 (C (/ (> p (q 99)) s) (r 99))))
!(size-atom (collapse (match &s (g $d $w $c) t)))
!(size-atom (collapse (match &s (h $d $w $c) t)))
; removal from the rows and from the relation, by variants and ground
!(remove-atom &s (g 0 3 (C (/ (> p (q 0)) (> $v $v)) (r 0))))
!(remove-atom &s (g 15 3 (C (/ (> p (q 19)) (> $v $v)) (r 19))))
!(remove-atom &s (h 0 7 (C (/ (> p (q 0)) s) (r 0))))
!(remove-atom &s (h 15 7 (C (/ (> p (q 19)) s) (r 19))))
!(size-atom (collapse (match &s (g $d $w $c) t)))
!(size-atom (collapse (match &s (h $d $w $c) t)))
!(size-atom (collapse (match &s (g $d 3 $c) t)))
!(size-atom (collapse (match &s (h $d 7 (C $p (r 19))) t)))
; the facts themselves, as bags
!(sort-atom (collapse (match &s (g $d 3 (C (/ (> p (q $k)) (> $v $v)) (r $k))) (if (== $d 0) $k (empty)))))
!(sort-atom (collapse (match &s (h 15 $w (C (/ (> p (q 19)) s) (r 19))) $w)))
!(sort-atom (collapse (match &s (g 0 0 (C (/ (> p (q $k)) $o) (r $k))) $k)))
