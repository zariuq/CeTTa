; Seeded scenarios for transform in the HE dialect (lib/lib_transform.metta).
; Each draws a store, copies of an atom among its atoms, and four transforms
; from a menu of every source form and both destinations, and prints each
; transform's answers and the store after it, sorted.  Run with the full authored
; implementation in tests/reference it must print the same.  Joint
; conditions (absent, unique and collect of a pattern headed by `,`) are among
; the forms.
!(import! &self lib_transform)

(= (next $s) (% (* $s 48271) 2147483647))
(= (pick $s $xs) (index-atom $xs (% $s (size-atom $xs))))

; (sort-atom takes its list as written, so the list is collected first)
(= (sorted $space) (let $atoms (collapse (get-atoms $space)) (sort-atom $atoms)))

(= (atom-of $s1 $s2 $s3)
   (let $x (pick $s1 (1 2 3))
     (let $y (pick $s2 (1 2 3))
       (pick $s3 ((p $x) (p $x) (q $x) (q $y) (r $x $y) (r $x $x))))))

; n atoms, copies allowed: HE's spaces keep every copy added (add-atom takes its
; atom as written, so each is drawn first)
(= (fill $space $s $n)
   (if (== $n 0) $s
     (let $s1 (next $s)
       (let $s2 (next $s1)
         (let $s3 (next $s2)
           (let $a (atom-of $s1 $s2 $s3)
             (let $_ (add-atom $space $a)
               (fill $space $s3 (- $n 1)))))))))

(= (run 0 $s) (transform $s (take (p $x)) (got $x)))
(= (run 1 $s) (transform $s (, (p $x) (q $x)) (both $x)))
(= (run 2 $s)
   (let $d (new-space)
     (let $n (transform $s (, (take (p $x)) (q $x)) (moved $x) $d)
       (into $n (sorted $d)))))
(= (run 3 $s) (transform $s (, (take (p $x)) (take (p $x))) (two $x)))
(= (run 4 $s) (transform $s (, (p $x) (absent (q $x))) (lonely $x)))
(= (run 5 $s) (transform $s (unique (p $x)) (only $x)))
(= (run 6 $s) (transform $s (collect (q $x) $all) (qs $all)))
(= (run 7 $s) (transform $s (collect (take (q $x)) $all) (took $all)))
(= (run 8 $s) (transform $s (, (p $x) (let $y (+ $x 1)) (q $y)) (succ $x $y)))
(= (run 9 $s) (transform $s (all (take (r $x $y))) (gone $x $y)))
(= (run 10 $s) (transform $s (, (p $x) (once (q $y))) (pair $x $y)))
(= (run 11 $s) (transform $s (, (take (p $x)) (let $z (* $x 2))) (, (p2 $z) (seen $x))))
(= (run 12 $s) (transform $s (read (r $x $x)) (diagonal $x)))
(= (run 13 $s) (transform $s (take (r $x $y)) (r $y $x) $s))
(= (run 14 $s) (transform $s (, (take (q $x)) (let $w (unknown $x))) (w $w)))
(= (run 15 $s) (transform $s (, (p $x) (absent (, (q $x) (r $x $x)))) (no-pair $x)))
(= (run 16 $s) (transform $s (unique (take (, (p $x) (q $x)))) (one-pair $x)))
(= (run 17 $s) (transform $s (collect (, (p $x) (q $x)) $all) (pairs $all)))
(= (run 18 $s) (transform $s (collect (take (, (q $x) (r $x $y))) $c) (took-pairs $c)))

(= (step $space $s)
   (let $k (% (next $s) 19)
     (let $all (collapse (run $k $space))
      (let $answers (sort-atom $all)
       (ran $k $answers (sorted $space))))))

(= (steps $space $s $n)
   (if (== $n 0) ()
     (let $r (step $space $s)
       (let $rest (steps $space (next (next $s)) (- $n 1))
         (cons-atom $r $rest)))))

(= (scenario $seed)
   (let $space (new-space)
     (let $s (fill $space (next $seed) 8)
       (let $start (sorted $space)
         (let $ran (steps $space (next $s) 4)
           (scenario $seed $start $ran))))))

(= (scenarios $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (scenario $seed))
       (scenarios (next (next (next $seed))) (- $n 1)))))

!(scenarios 1 120)
