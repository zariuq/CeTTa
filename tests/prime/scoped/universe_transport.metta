; Identity elimination at a universe carrier transports a term along a path
; between types.  The language declares the eliminator with two universe
; levels, the carrier's and the motive's, and each use of it chooses its own.
; With the motive (lam Z (lam e Z)) the method is a term of the start type and
; the result a term of the end type.  At reflexivity the transport is its
; input; along a path that is not reflexivity it stays as it is.

!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &s (: A (u 0)))
!(add-atom &s (: B (u 0)))
!(add-atom &s (: p (id (u 0) A B)))
!(add-atom &s (: a A))
!(add-atom &s (: b B))

; Along a declared path a term of A becomes a term of B.
!(type:of &s (id:eliminate (u 0) A (lam Z (lam e Z)) a B p))
!(type:check &s (id:eliminate (u 0) A (lam Z (lam e Z)) a B p) B)
!(type:check &s (id:eliminate (u 0) A (lam Z (lam e Z)) a B p) A)
!(type:eq &s (id:eliminate (u 0) A (lam Z (lam e Z)) a B p) a)
!(type:eq &s (id:eliminate (u 0) A (lam Z (lam e Z)) a B p) b)

; At reflexivity the transport is its input, also at the numbers.
!(type:of &s (id:eliminate (u 0) A (lam Z (lam e Z)) a A (refl A)))
!(type:eq &s (id:eliminate (u 0) A (lam Z (lam e Z)) a A (refl A)) a)
!(type:of &s (id:eliminate (u 0) num (lam Z (lam e Z)) zero num (refl num)))
!(type:eq &s (id:eliminate (u 0) num (lam Z (lam e Z)) zero num (refl num)) zero)

; As a definition its body is checked in the context of its parameters,
; A B : (u 0), p : (id (u 0) A B), a : A, at the end type B.
!(set:define &s transport
   (-> (A : (u 0)) (B : (u 0)) (p : (id (u 0) A B)) (a : A) B)
   (= (transport $A $B $p $a) (id:eliminate (u 0) $A (lam Z (lam e Z)) $a $B $p)))
!(type:check &s (transport A B p a) B)
!(type:eq &s (transport A B p a) a)
!(type:check &s (transport A A (refl A) a) A)
!(type:eq &s (transport A A (refl A) a) a)
!(type:eq &s (transport num num (refl num) (suc zero)) (suc zero))
; The body at the start type is refused.
!(set:define &s transportBack
   (-> (A : (u 0)) (B : (u 0)) (p : (id (u 0) A B)) (a : A) A)
   (= (transportBack $A $B $p $a) (id:eliminate (u 0) $A (lam Z (lam e Z)) $a $B $p)))

; In the open context X : (u 0), d : X, transport along reflexivity is d.
!(set:define &s transportRefl
   (-> (X : (u 0)) (d : X) X)
   (= (transportRefl $X $d) (id:eliminate (u 0) $X (lam Z (lam e Z)) $d $X (refl $X))))
!(type:check &s (lam X (lam d (refl d)))
   (-> (X : (u 0)) (d : X)
       (id X (id:eliminate (u 0) X (lam Z (lam e Z)) d X (refl X)) d)))
!(set:define &s reflComputes
   (-> (X : (u 0)) (d : X) (id X (transportRefl X d) d))
   (= (reflComputes $X $d) (refl $d)))
; Along a loop that is not reflexivity it does not compute.
!(type:check &s (lam X (lam q (lam d (refl d))))
   (-> (X : (u 0)) (q : (id (u 0) X X)) (d : X)
       (id X (id:eliminate (u 0) X (lam Z (lam e Z)) d X q) d)))

; A carrier in (u 1): the same transport one universe up.
!(set:define &s transport1
   (-> (A : (u 1)) (B : (u 1)) (p : (id (u 1) A B)) (a : A) B)
   (= (transport1 $A $B $p $a) (id:eliminate (u 1) $A (lam Z (lam e Z)) $a $B $p)))
!(type:check &s (transport1 (u 0) (u 0) (refl (u 0)) num) (u 0))
!(type:eq &s (transport1 (u 0) (u 0) (refl (u 0)) num) num)
; The types of (u 0) are types of (u 1), so it also transports numbers.
!(type:check &s (transport1 num num (refl num) zero) num)
!(type:eq &s (transport1 num num (refl num) zero) zero)
!(type:check &s (id:eliminate (u 1) num (lam Z (lam e Z)) zero num (refl num)) num)
; A path between types of (u 0) is no path at the carrier (u 1).
!(type:check &s p (id (u 1) A B))
!(type:check &s (transport1 A B p a) B)

; One instance, carrier level 1 and motive level 0, serves the levels below it:
; the carrier (u 0) and the carrier num.
!(set:define &s J1
   (-> (A : (u 1)) (x : A) (P : (-> (y : A) (-> (id A x y) (u 0))))
       (d : (P x (refl x))) (y : A) (e : (id A x y)) (P y e))
   (= (J1 $A $x $P $d $y $e) (id:eliminate $A $x $P $d $y $e)))
!(type:check &s (J1 (u 0) A (lam Z (lam e Z)) a B p) B)
!(type:check &s (J1 (u 0) num (lam Z (lam e Z)) zero num (refl num)) num)
!(type:eq &s (J1 (u 0) num (lam Z (lam e Z)) zero num (refl num)) zero)
!(type:check &s (J1 num zero (lam y (lam e num)) zero zero (refl zero)) num)
!(type:eq &s (J1 num zero (lam y (lam e num)) zero zero (refl zero)) zero)
; The instance at the lowest universes has its carrier in (u 0), which does not
; contain (u 0): it refuses the transport at the numbers.
!(set:define &s J0
   (-> (A : (u 0)) (x : A) (P : (-> (y : A) (-> (id A x y) (u 0))))
       (d : (P x (refl x))) (y : A) (e : (id A x y)) (P y e))
   (= (J0 $A $x $P $d $y $e) (id:eliminate $A $x $P $d $y $e)))
!(type:check &s (J0 (u 0) num (lam Z (lam e Z)) zero num (refl num)) num)
!(type:check &s (J0 num zero (lam y (lam e num)) zero zero (refl zero)) num)

; A motive returns a type: at the carrier num, (lam Z (lam e Z)) returns a number.
!(type:check &s (lam Z (lam e Z)) (-> (y : num) (-> (id num zero y) (u 0))))
!(type:check &s (id:eliminate num zero (lam Z (lam e Z)) zero zero (refl zero)) num)
!(type:eq &s (id:eliminate num zero (lam Z (lam e Z)) zero zero (refl zero)) zero)

; (refl (u 0)) is no path from (u 1) to (u 0), and nothing computes on it.
!(type:check &s (refl (u 0)) (id (u 2) (u 1) (u 0)))
!(type:check &s (id:eliminate (u 2) (u 1) (lam y (lam e y)) num (u 0) (refl (u 0))) (u 0))
!(type:eq &s (id:eliminate (u 2) (u 1) (lam y (lam e y)) num (u 0) (refl (u 0))) num)
; Along (refl (u 1)) the same eliminator computes.
!(type:check &s (id:eliminate (u 2) (u 1) (lam y (lam e y)) num (u 1) (refl (u 1))) (u 1))
!(type:eq &s (id:eliminate (u 2) (u 1) (lam y (lam e y)) num (u 1) (refl (u 1))) num)

; A mismatched endpoint, a wrong input and a wrong result type fail.
!(type:check &s (id:eliminate (u 0) A (lam Z (lam e Z)) a B (refl A)) B)
!(type:check &s (transport A B (refl A) a) B)
!(type:check &s (transport A A (refl A) zero) A)
!(type:check &s (transport A B p a) A)
