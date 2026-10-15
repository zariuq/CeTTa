; A type can be declared before its members.  Declared with no constructors
; it is a set about which nothing else is known: it can be used at once and
; given members, and `set:inductive` closes it, making its declared constants
; constructors.  Closing is a further commitment: the closed type admits
; definitions by cases and refuses another closure.

; The closed empty type: a datatype with no constructors.  Its principle has
; no cases and its recursor no methods; with no constructors it has no
; computation rule, so its operational face is empty.
!(set:inductive Void (u 0))
!(match &self (set:known Void-ind $p $o $f $d $r $s) $p)
!(match &self (: Void-rec (-> (: $P (-> Void $sort)) (-> (: $t Void) ($P $t)))) no-methods)
!(set:theorem void-elim (all Void (lam v Falsum))
    (pf:all-elim (pf:known Void-ind) (lam v Falsum)))
!(type:check (lam v (Void-rec (lam w Void) v)) (-> Void Void))

; An open type, used before it has constructors.
(: Shape (u 0))
(: circle Shape)
!(set:define shape-id (-> Shape Shape) (= (shape-id $x) $x))
!(shape-id circle)
!(car-atom (try (set:define shape-code (-> Shape Shape) (= (shape-code circle) circle))))

; Closing it: the declared constant becomes a constructor and square is new.
!(set:inductive Shape (u 0) (: circle Shape) (: square Shape))
!(match &self (: Shape $t) $t)
!(match &self (: circle $t) $t)
!(car-atom (try (set:recheck Shape)))
!(set:define shape-swap (-> Shape Shape)
    (= (shape-swap circle) square) (= (shape-swap square) circle))
!(shape-swap circle)
!(shape-id square)
!(type:check (Shape-rec (lam s Shape) square circle circle) Shape)
!(type:eq (Shape-rec (lam s Shape) square circle circle) square)

; A closed type is not closed again with another constructor.
!(car-atom (try (set:inductive Shape (u 0) (: circle Shape) (: square Shape) (: triangle Shape))))

; A name declared at another universe is not closed at (u 0).
(: Big (u 1))
!(car-atom (try (set:inductive Big (u 0) (: big-one Big))))

; A constant declared at another type is not made a constructor, and the
; type stays open.
(: Tone (u 0))
(: loud (-> Tone Tone))
!(car-atom (try (set:inductive Tone (u 0) (: loud Tone))))
!(match &self (: Tone $t) $t)
!(car-atom (try (set:define tone-code (-> Tone Tone) (= (tone-code (loud $x)) $x))))
