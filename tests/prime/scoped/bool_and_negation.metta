; Booleans, with the safeguard that a definition needs a datatype, not an
; induction-shaped statement: negation is not a constructor, so a "height"
; that steps on negation is refused, although Booleans do satisfy an
; induction principle with false as zero and negation as successor.
!(import! &self pf)
!(bind! &thy (new-space))
!(set:inductive &thy bool (u 0) (: true bool) (: false bool))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(pf:equality &thy bool refl@bool subst@bool)
!(set:define &thy not (-> bool bool) (= (not true) false) (= (not false) true))
!(set:define &thy and (-> bool bool bool) (= (and true $b) $b) (= (and false $b) false))
; not (not b) = b, by cases: both are computations.
!(pf:theorem &thy not-not (all bool (lam b (eq bool (not (not b)) b)))
   (pf:induction bool-ind (lam b (eq bool (not (not b)) b))
     (pf:refl bool true)
     (pf:refl bool false)))
; the induction principle with negation as successor can be stated and even
; proved, since every Boolean is false or the negation of false...
!(pf:theorem &thy neg-induction
   (all (-> bool prop) (lam P (imp (P false) (imp (all bool (lam b (imp (P b) (P (not b))))) (all bool (lam b (P b)))))))
   (pf:fix P (pf:assume base (pf:assume step
     (pf:induction bool-ind (lam b (P b))
       (pf:imp-elim (pf:all-elim step false) base)
       base)))))
; ...but it justifies no recursive definition: `height (not x) = succ (height x)`
; is refused, because not is not a constructor of bool.
!(set:define &thy height (-> bool nat)
   (= (height false) zero)
   (= (height (not $x)) (succ (height $x))))
; and a definition over an induction-shaped axiom alone is refused too.
!(pf:theory &thy ((: word (u 0)) (: empty word) (: step (-> word word))))
!(set:axiom &thy word-ind (all (-> word prop) (lam P (imp (P empty) (imp (all word (lam w (imp (P w) (P (step w))))) (all word (lam w (P w))))))))
!(set:define &thy len (-> word nat) (= (len empty) zero) (= (len (step $w)) (succ (len $w))))
!(let $r (try (set:define &thy len (-> word nat) (= (len empty) zero) (= (len (step $w)) (succ (len $w))))) (let $e (index-atom $r 1) (car-atom $e)))
