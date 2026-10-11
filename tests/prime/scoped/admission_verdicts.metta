(set:profile hol)

; A definition or datatype that is legal but not yet admitted is answered
; Undetermined.  A refusal needs a witness that no evidence admits it: a
; value that would contain itself, a non-positive field, a constructor of
; another type.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:inductive &s bool (u 0) (: true bool) (: false bool))

; Legal, not admitted yet: two inspected arguments, a nested pattern, a
; variable beside constructor patterns, two equations for one constructor.
!(let $r (try (set:define &s eqn (-> nat nat bool)
                (= (eqn zero zero) true) (= (eqn zero (succ $m)) false)
                (= (eqn (succ $n) zero) false) (= (eqn (succ $n) (succ $m)) (eqn $n $m))))
   (car-atom $r))
!(let $r (try (set:define &s half (-> nat nat)
                (= (half zero) zero) (= (half (succ zero)) zero)
                (= (half (succ (succ $n))) (succ (half $n)))))
   (car-atom $r))
!(let $r (try (set:define &s is-zero (-> nat bool)
                (= (is-zero zero) true) (= (is-zero $n) false)))
   (car-atom $r))
!(let $r (try (set:define &s twice-zero (-> nat bool)
                (= (twice-zero zero) true) (= (twice-zero zero) false)
                (= (twice-zero (succ $n)) false)))
   (car-atom $r))

; Legal, not admitted yet: recursion that terminates, on an argument that
; is not a constructor field.
!(set:define &s pred (-> nat nat) (= (pred zero) zero) (= (pred (succ $n)) $n))
!(let $r (try (set:define &s down (-> nat nat)
                (= (down zero) zero) (= (down (succ $n)) (down (pred (succ $n))))))
   (car-atom $r))

; Legal, not admitted yet: a strictly positive function field, a dependent
; field, an indexed family, a datatype with a parameter that occurs nested
; in its own constructors.
!(let $r (try (set:inductive &s ord (u 0) (: oz ord) (: olim (-> (-> nat ord) ord))))
   (car-atom $r))
!(let $r (try (set:inductive &s dpair (u 0) (: mk (-> (b : bool) nat dpair))))
   (car-atom $r))
!(let $r (try (set:inductive &s vec (-> nat (u 0))
                (: vnil (vec zero)) (: vcons (-> nat (vec $n) (vec (succ $n))))))
   (car-atom $r))
!(let $r (try (set:inductive &s (nlist (: A (u 0))) (u 0)
                (: nnil (nlist A)) (: ncons (-> A (nlist (nlist A)) (nlist A)))))
   (car-atom $r))
; A datatype with a parameter that every constructor uses at exactly that
; parameter is admitted.
!(let $r (try (set:inductive &s (plist (: A (u 0))) (u 0)
                (: pnil (plist A)) (: pcons (-> A (plist A) (plist A)))))
   (car-atom $r))

; Legal, not admitted yet: calls that repeat their own left side never stop
; when run, but some set satisfies these equations (a constant function), and
; a missing case leaves the value there open.
!(let $r (try (set:define &s climb (-> nat nat)
                (= (climb zero) zero) (= (climb (succ $n)) (climb (succ (succ $n))))))
   (car-atom $r))
!(let $r (try (set:define &s spin (-> nat nat)
                (= (spin zero) zero) (= (spin (succ $n)) (spin (succ $n)))))
   (car-atom $r))
!(let $r (try (set:define &s partial (-> bool nat) (= (partial true) zero)))
   (car-atom $r))

; The numbers from n, as a list and as a stream.  As a list the value of
; from n would contain the value of from (suc n), and so on without end,
; which no list does: refused, with that witness.  As a stream, a function
; from positions, the same equation has exactly one solution: undetermined,
; with the routes that may admit it.
!(set:inductive &s list (u 0) (: nil list) (: cons (-> nat list list)))
!(try (set:define &s from (-> nat list) (= (from $n) (cons $n (from (succ $n))))))
!(set:define &s scons (-> nat (-> nat nat) (-> nat nat))
   (= (scons $a $s zero) $a)
   (= (scons $a $s (succ $k)) ($s $k)))
!(try (set:define &s stream-from (-> nat (-> nat nat))
        (= (stream-from $n) (scons $n (stream-from (succ $n))))))
; A list that holds itself at another argument is refused the same way.
!(try (set:define &s from-two (-> nat list) (= (from-two $n) (cons zero (from-two (succ (succ $n)))))))

; A proposition equal to its own negation has no truth value: refused, but
; only when the call in the negation is the left side itself.  At another
; instance the equation may have a solution: alt is true at the even numbers
; and false at the odd ones, and is undetermined.  So is d, whose call is at
; a constant: it has no solution, which the left-side shape does not show.
!(try (set:define &s selfneg (-> nat prop) (= (selfneg $n) (imp (selfneg $n) Falsum))))
!(try (set:define &s cz prop (= cz (imp cz Falsum))))
!(try (set:define &s alt (-> nat prop) (= (alt $n) (imp (alt (succ $n)) Falsum))))
!(try (set:define &s d (-> nat prop) (= (d $n) (imp (d zero) Falsum))))
; Over a type with no value, every equation holds: void's one constructor
; takes a void.  Neither shape is refuted there.
!(set:inductive &s void (u 0) (: grow (-> void void)))
!(try (set:define &s vneg (-> void prop) (= (vneg $v) (imp (vneg $v) Falsum))))
!(try (set:define &s vlist (-> void list) (= (vlist $v) (cons zero (vlist $v)))))

; Refused with a witness: a non-positive field, a constructor of another type.
!(let $r (try (set:inductive &s bad (u 0) (: mkbad (-> (-> bad nat) bad))))
   (car-atom $r))
!(let $r (try (set:inductive &s odd (u 0) (: mkodd (-> nat nat))))
   (car-atom $r))

; The reason is named in the verdict.
!(try (set:define &s spin (-> nat nat)
        (= (spin zero) zero) (= (spin (succ $n)) (spin (succ $n)))))
!(try (set:define &s partial (-> bool nat) (= (partial true) zero)))

; Nothing refused or undetermined is published.
!(== (collapse (match &s (: eqn $t) $t){$t}) ())
!(== (collapse (match &s (: spin $t) $t){$t}) ())
