; A definition or datatype that is legal but not yet admitted is answered
; Undetermined.  A refusal needs a witness: a loop, a missing case, a
; non-positive field, a constructor of another type.
!(bind! &s (new-space))
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
; field, an indexed family, a parameter.
!(let $r (try (set:inductive &s ord (u 0) (: oz ord) (: olim (-> (-> nat ord) ord))))
   (car-atom $r))
!(let $r (try (set:inductive &s dpair (u 0) (: mk (-> (b : bool) nat dpair))))
   (car-atom $r))
!(let $r (try (set:inductive &s vec (-> nat (u 0))
                (: vnil (vec zero)) (: vcons (-> nat (vec $n) (vec (succ $n))))))
   (car-atom $r))
!(let $r (try (set:inductive &s (plist (: A (u 0))) (u 0)
                (: pnil (plist A)) (: pcons (-> A (plist A) (plist A)))))
   (car-atom $r))

; Refused with a witness: calls that repeat their own left side loop.
!(let $r (try (set:define &s climb (-> nat nat)
                (= (climb zero) zero) (= (climb (succ $n)) (climb (succ (succ $n))))))
   (car-atom $r))
!(let $r (try (set:define &s spin (-> nat nat)
                (= (spin zero) zero) (= (spin (succ $n)) (spin (succ $n)))))
   (car-atom $r))
!(let $r (try (set:define &s partial (-> bool nat) (= (partial true) zero)))
   (car-atom $r))
!(let $r (try (set:inductive &s bad (u 0) (: mkbad (-> (-> bad nat) bad))))
   (car-atom $r))
!(let $r (try (set:inductive &s odd (u 0) (: mkodd (-> nat nat))))
   (car-atom $r))

; The witness is named in the verdict.
!(try (set:define &s spin (-> nat nat)
        (= (spin zero) zero) (= (spin (succ $n)) (spin (succ $n)))))
!(try (set:define &s partial (-> bool nat) (= (partial true) zero)))

; Nothing refused or undetermined is published.
!(== (collapse (match &s (: eqn $t) $t)) ())
!(== (collapse (match &s (: spin $t) $t)) ())
