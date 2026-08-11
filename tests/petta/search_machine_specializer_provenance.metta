(= (p1 $x) (+ 1 $x))
(= (tag $x) (Tagged $x))

(: mixed (-> Atom Atom Atom %Undefined%))

; This general clause admits the higher-order parameter for specialization
; without contributing an answer.  The specific clause below then checks that
; the one derived pattern can carry both structural and relational roles.
(= (mixed $f $a $b)
   (if False ($f 0) (empty)))

; The selected higher-order application in the specialized head is data:
; it must destructure `(p1 (1 2))` rather than call `p1` on the list.
; The authored `tag` application beside it remains a relational constraint.
(= (mixed $f ($f (cons $x $xs)) (tag $x))
   ($f $x))

!(mixed p1 (p1 (1 2)) (Tagged 1))

; A non-matching authored relational constraint must not gain an answer merely
; because the adjacent specialized application is structural.
!(mixed p1 (p1 (1 2)) (Other 1))

; Specialization sees the values delivered to a call, not the source syntax
; that computes those values.  In particular, bt-fold receives the Boolean
; list produced by bt-map; the nested head `bt-map` is never a list element.
(= (bt-map $f ()) ())
(= (bt-map $f (cons $x $xs))
   (cons ($f $x) (bt-map $f $xs)))
(= (bt-fold $f $init ()) $init)
(= (bt-fold $f $init (cons $x $xs))
   (bt-fold $f ($f $init $x) $xs))

!(bt-fold or false (bt-map (= 1) (1 2)))
!(bt-fold and true (bt-map (= 1) (1 2)))
