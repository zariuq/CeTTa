; Definitions and datatypes are admitted and compute at any size: no fixed
; bound on arguments, equations, constructors, fields or pattern variables
; stands in for a verdict.
!(bind! &s (new-space))
!(set:inductive &s nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:inductive &s bool (u 0) (: true bool) (: false bool))

; A function of seventeen arguments computes by its rule; the conversion of
; its application with the reduct is established, with anything else refuted.
!(set:define &s pick17 (-> nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat)
   (= (pick17 $a1 $a2 $a3 $a4 $a5 $a6 $a7 $a8 $a9 $a10 $a11 $a12 $a13 $a14 $a15 $a16 $a17) $a17))
!(type:eq &s (pick17 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) (succ zero))
!(type:eq &s (pick17 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) zero)
!(set:eq &s (pick17 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) (succ zero))
!(type:check &s (refl (succ zero))
   (id nat (pick17 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) (succ zero)))

; Nine constructors, and a definition by nine equations over them.
!(set:inductive &s nine (u 0) (: c1 nine) (: c2 nine) (: c3 nine) (: c4 nine) (: c5 nine)
   (: c6 nine) (: c7 nine) (: c8 nine) (: c9 nine))
!(set:define &s is-last (-> nine bool)
   (= (is-last c1) false) (= (is-last c2) false) (= (is-last c3) false)
   (= (is-last c4) false) (= (is-last c5) false) (= (is-last c6) false)
   (= (is-last c7) false) (= (is-last c8) false) (= (is-last c9) true))
!(type:eq &s (is-last c9) true)
!(type:eq &s (is-last c8) true)
!(type:check &s nine-rec
   (-> (P : (-> nine (u 0))) (m1 : (P c1)) (m2 : (P c2)) (m3 : (P c3)) (m4 : (P c4))
       (m5 : (P c5)) (m6 : (P c6)) (m7 : (P c7)) (m8 : (P c8)) (m9 : (P c9))
       (t : nine) (P t)))

; A constructor with nine fields, taken apart by a pattern of nine variables
; beside eight more arguments: seventeen pattern variables in one equation.
!(set:inductive &s wide (u 0) (: mkwide (-> nat nat nat nat nat nat nat nat nat wide)))
!(set:define &s ninth (-> nat nat nat nat nat nat nat nat wide nat)
   (= (ninth $b1 $b2 $b3 $b4 $b5 $b6 $b7 $b8 (mkwide $f1 $f2 $f3 $f4 $f5 $f6 $f7 $f8 $f9)) $f9))
!(type:eq &s (ninth zero zero zero zero zero zero zero zero
               (mkwide zero zero zero zero zero zero zero zero (succ zero)))
   (succ zero))
!(type:eq &s (ninth zero zero zero zero zero zero zero zero
               (mkwide zero zero zero zero zero zero zero zero (succ zero)))
   zero)
