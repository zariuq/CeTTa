(scope:profile query-wide)

; The scope-policy spectrum corpus, under the profile declared at the top of
; this file.  L = (lam z (let $y z (g $y))), K = (lam z (Pair z $y)).  A
; crossing set is braces touching the template: (lam z body){$t},
; (map-atom L $e T){$t}.  Under every profile it decides the template's
; names: the set's names are shared with the scope around, and the template
; owns every other name its region uses (the profile's default applies only
; where no set is written).  Each row is labelled; a row with no answer
; prints only its label.  A name's #suffix is renumbered per line by the fixture
; comparison.

(= (mk) (lam z (let $y z (g $y))))
(= (mk-open) (lam z (Pair z $hole)))
(= (second $s $t) (let (d $s $t) (d a b) (Got $t)))
(= (second-u $s $t) (unify (d $s $t) (d a b) (Got $t) (empty)))
(= (lifted $y $z) (let $y $z (g $y)))
(= (Lf $z) (let $y $z (g $y)))

; 1-3: independent locals across calls, side-by-side inlining, the same
; argument twice.
!(row 1)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))
!(row 2)
!(Pair ((lam z (let $y z (g $y))) 1) ((lam z (let $y z (g $y))) 2))
!(row 3)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) ($f 1)))

; 4-6: a same-spelled outsider, read; bound before the definition; bound
; elsewhere in the scope.  The w variants rename the outsider; 4ec lists $y,
; 4u refines by unify, 4L2 renames L's private name.
!(row 4)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) $y))
!(row 4w)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) $w))
!(row 4ec)
!(let $f (lam z (let $y z (g $y))){$y} (Pair ($f 1) $y))
!(row 4u)
!(let $f (lam z (unify $y z (g $y) (empty))) (Pair ($f 1) $y))
!(row 4L2)
!(let $f (lam z (let $y2 z (g $y2))) (Pair ($f 1) $y))
!(row 5)
!(let $y 5 (let $f (lam z (let $y z (g $y))) ($f 1)))
!(row 5w)
!(let $w 5 (let $f (lam z (let $y z (g $y))) ($f 1)))
!(row 6)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) (let $y 5 $y)))
!(row 6w)
!(let $f (lam z (let $y z (g $y))) (Pair ($f 1) (let $w 5 $w)))

; 7-8: capture, the call before and after $y := 5; u: explicit unify; ec: K
; lists $y.
!(row 7)
!(let $f (lam z (Pair z $y)) (let $r ($f 1) (let $y 5 $r)))
!(row 8)
!(let $f (lam z (Pair z $y)) (let $y 5 (let $r ($f 1) $r)))
!(row 7u)
!(let $f (lam z (Pair z $y)) (let $r ($f 1) (unify $y 5 $r (empty))))
!(row 8u)
!(let $f (lam z (Pair z $y)) (unify $y 5 (let $r ($f 1) $r) (empty)))
!(row 7ec)
!(let $f (lam z (Pair z $y)){$y} (let $r ($f 1) (let $y 5 $r)))
!(row 8ec)
!(let $f (lam z (Pair z $y)){$y} (let $y 5 (let $r ($f 1) $r)))

; 9: an intentional captured hole, output through a closure; 9w renames the
; outsider; 9u refines by unify.  14: row 9 with explicit capture.
!(row 9)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))
!(row 9w)
!(let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $u $r)))
!(row 9u)
!(let $f (lam z (unify (d $t) z ok (empty))) (let $r ($f (d 7)) (Pair $t $r)))
!(row 14)
!(let $f (lam z (let (d $t) z ok)){$t} (let $r ($f (d 7)) (Pair $t $r)))

; 10: nested templates.
!(row 10a)
!((lam z (Pair (let $y z $y) ((lam w (let $y w $y)) 3))) 3)
!(row 10b)
!((lam z (Pair (let $y z $y) ((lam w (let $y w $y)) 3))) 4)

; 11: inlining with binder renaming (hygiene): the let form, the text
; inlined, inlined with L's private name renamed apart, lam w listing $f.
!(row 11)
!(let $f (lam z (let $y z (g $y))) ((lam w (Pair ($f 1) (let $y w $y))) 5))
!(row 11i)
!((lam w (Pair ((lam z (let $y z (g $y))) 1) (let $y w $y))) 5)
!(row 11h)
!((lam w (Pair ((lam z (let $y2 z (g $y2))) 1) (let $y w $y))) 5)
!(row 11ec)
!(let $f (lam z (let $y z (g $y))) ((lam w (Pair ($f 1) (let $y w $y))){$f} 5))

; 12: template capture, binding after and before; u: unify; ec: the
; template lists $y; m: a lambda as the template.
!(row 12a)
!(let $r (map-atom (1 2) $e (Pair $e $y)) (let $y 5 $r))
!(row 12b)
!(let $y 5 (map-atom (1 2) $e (Pair $e $y)))
!(row 12au)
!(let $r (map-atom (1 2) $e (Pair $e $y)) (unify $y 5 $r (empty)))
!(row 12bu)
!(unify $y 5 (map-atom (1 2) $e (Pair $e $y)) (empty))
!(row 12aec)
!(let $r (map-atom (1 2) $e (Pair $e $y)){$y} (let $y 5 $r))
!(row 12bec)
!(let $y 5 (map-atom (1 2) $e (Pair $e $y)){$y})
!(row 12am)
!(let $r (let $tm (lam e (Pair e $y)) (list2 ($tm 1) ($tm 2))) (let $y 5 $r))
!(row 12bm)
!(let $y 5 (let $tm (lam e (Pair e $y)) (list2 ($tm 1) ($tm 2))))

; 13: a returned closure keeps its local slot; library immunity; two
; closures from two calls of (mk).
!(row 13a)
!(let $f (mk) (Pair ($f 1) ($f 2)))
!(row 13b)
!(let $f (mk) (Pair ($f 1) (let $y 5 $y)))
!(row mk2)
!(Pair (let $f (mk) ($f 1)) (let $p (mk) ($p 2)))

; 16: partial application.  18: one application read twice.
!(row 16)
!(let $p ((lam z (lam w (let $y (Pair z w) $y))) 1) (Pair ($p 2) ($p 3)))
!(row 18)
!(let $f (lam z (let $y z (g $y))) (let $a ($f 1) (Pair $a $a)))

; 17: the lifted equation of row 4's lambda (the captured $y a parameter),
; and the per-call lifted equation of L called twice.
!(row 17)
!(Pair (lifted $y 1) $y)
!(row LfEq)
!(Pair (Lf 1) (Lf 2))

; 19: failure and effect ordering: which prints happen (the prints come
; first in the output, in their order).
!(row 19a)
!(let $f (lam z (let $y z (let $u (println! (got-a $y)) (g $y)))) (Pair ($f 1) ($f 2)))
!(row 19b)
!(let $f (lam z (let $y z (let $u (println! (got-b $y)) (g $y)))) (Triple ($f 1) ($f 2) $y))

; 22: caller refinement (8/07), implicit and by unify.  24 is row 22a.
!(row 22a)
!(let $r (second a $t) (Pair $t $r))
!(row 22b)
!(second c $t)
!(row 22au)
!(let $r (second-u a $t) (Pair $t $r))
!(row 22bu)
!(second-u c $t)

; 23: relational coherence: sibling lets, two activations, a nested let of
; the same spelling, and with unify.
!(row 23a)
!(Pair (let $x 1 $x) (let $x 2 $x))
!(row 23b)
!(Pair ((lam z (let $y z (g $y))) 1) ((lam z (let $y z (g $y))) 2))
!(row 23c)
!(let $y 1 (Pair $y (let $y 2 $y)))
!(row 23cu)
!(let $y 1 (Pair $y (unify $y 2 $y (empty))))

; 25: two private same-spelled locals and one connecting lambda; ec: it
; lists $t.
!(row 25)
!(Pair (Pair ((lam z (let $y z $y)) 1) ((lam z (let $y z $y)) 2)) (let $f (lam z (let $t z ok)) (let $r ($f 3) (Pair $r $t))))
!(row 25ec)
!(Pair (Pair ((lam z (let $y z $y)) 1) ((lam z (let $y z $y)) 2)) (let $f (lam z (let $t z ok)){$t} (let $r ($f 3) (Pair $r $t))))

; 26: two associations kept as alternatives (superpose), neither forced.
!(row 26)
!(superpose ((let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r))) (let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))))
!(row 26ec)
!(superpose ((let $f (lam z (let (d $t) z ok)){$t} (let $r ($f (d 7)) (Pair $t $r))) (let $f (lam z (let (d $t) z ok)) (let $r ($f (d 7)) (Pair $t $r)))))

; 27: built, open code connected to a new context through its result,
; without rebinding the closure.
!(row 27)
!(let $f (mk-open) (let $r ($f 1) (let (Pair 1 6) $r $r)))

; 28: an authored query extended with a same-spelled outsider is
; re-elaborated (row 4); a built closure extended so is not.
!(row 28)
!(let $f (mk) (Pair ($f 1) $y))

; 21: module and profile isolation.  Each library declares its own profile;
; its closures are applied here by their own records.
!(row 21)
!(import! &self ./lib/scope_lib_query_wide)
!(import! &self ./lib/scope_lib_explicit_capture)
!(row 21qw)
!(let $f (lib-qw) (Pair ($f 1) ($f 2)))
!(row 21ec)
!(let $f (lib-ec) (Pair ($f 1) ($f 2)))
!(row 21ecs)
!(let $f (lib-ec-shared) (let $r ($f (d 7)) (Pair $t $r)))
