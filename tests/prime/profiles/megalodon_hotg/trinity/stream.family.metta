(set:profile megalodon-hotg)

; The numbers from n as a stream of a declared family, on three faces.  The
; family is the one Lean declares over the tower inside the sets:
;   Stream : set -> set
;   shead A (scons A a s) = a        stail A (scons A a s) = s
;   iter A f a = scons A a (iter A f (f a))
; It is admitted on the set model Lean gives it: the streams over a set A
; are the functions from the numbers into A, and the three equations hold
; between those sets.  The third equation makes a new call of iter on every
; use, and its rule unfolds only where shead or stail observe the call: a
; stream is never unfolded by itself, and every observation stops.  Two
; streams are compared by what is observed of them: conversion of two
; different stream terms decides nothing, and that their observations agree
; at every position is a theorem.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:known-proposition &self num-ind)
!(pf:equality &self num refl@num subst@num)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $k)) (suc (add $n $k))))

; ---- The family, written as Lean writes it.
!(set:family &self
   (: Stream (-> set set))
   (: scons (-> (A : set) (a : A) (s : (Stream A)) (Stream A)))
   (: shead (-> (A : set) (s : (Stream A)) A))
   (: stail (-> (A : set) (s : (Stream A)) (Stream A)))
   (: iter (-> (A : set) (f : (-> A A)) (a : A) (Stream A)))
   (= (shead $A (scons $A $a $s)) $a)
   (= (stail $A (scons $A $a $s)) $s)
   (= (iter $A $f $a) (scons $A $a (iter $A $f ($f $a)))))
; Each constant's record holds the family and the set model it was admitted
; on, the Lean theorem, which holds relative to a chain of closed universes
; (cofinally many inaccessibles), with the hypotheses the draft takes as
; given.  The equations are rules; the rule of iter waits for an observation.
!(set:known-proof &self iter)
!(match &self (type:rule shead $n $p $r) $n)
!(match &self (type:rule iter $n $p $r) $n)

; ---- The numbers from n, and the stream from position k on.
!(set:define &self from (-> num (Stream num)) (= (from $n) (iter num suc $n)))
!(set:define &self drop (-> num (Stream num) (Stream num))
   (= (drop zero $s) $s)
   (= (drop (suc $k) $s) (drop $k (stail num $s))))

; ---- Operational: a stream stays as it is; its observation at position 2
; from 1 runs to 3.  The firings are those of the equations: from once,
; iter under each of the three observations, stail twice and shead once.
; The count is printed with the term it counts.
!(from (suc zero))
!(stail num (from (suc zero)))
!(shead num (stail num (stail num (from (suc zero)))))
!(let (quote ($values $n)) (lift (cost:firings (shead num (stail num (stail num (from (suc zero)))))))
   (quote (firings-of (shead num (stail num (stail num (from (suc zero))))) $values $n)))
!(shead num (drop (suc (suc zero)) (from (suc zero))))

; A partial application is a value: stail at num, given no stream, is the
; function from streams to their tails, and iter takes it as its function.
; The stream of the tails of the numbers from 0 runs under observation: the
; head of its second element is 1.  A call with an argument too many, and a
; stream where a function is wanted, are still refused.
!(stail num)
!(iter (Stream num) (stail num) (from zero))
!(shead num (shead (Stream num) (stail (Stream num) (iter (Stream num) (stail num) (from zero)))))
!((stail num) (from zero))
!(stail num (from zero) zero)
!(iter num (from zero) zero)

; ---- Intensional: typed; an observation is a number, equal by computation
; at a closed position and at an unknown number.  With no observation,
; nothing unfolds: two different stream terms are not compared, whether or
; not they are equal as sets.
!(type:of &self from)
!(type:of &self (iter num suc zero))
!(type:of &self (shead num (stail num (from zero))))
!(type:eq &self (shead num (stail num (stail num (from (suc zero))))) (suc (suc (suc zero))))
!(add-atom &self (: m num))
!(type:eq &self (shead num (stail num (from m))) (suc m))
!(type:eq &self (stail num (from m)) (from (suc m)))
!(try (type:eq &self (from m) (scons num m (from (suc m)))))
!(try (type:eq &self (from zero) (from (suc zero))))

; ---- Extensional: a stream of numbers is a set; the observation at
; position k of the stream from n is n + k, by induction on k for every n.
!(set:formed &self (Stream num))
!(set:of &self (shead num (from zero)))
!(pf:theorem &self add-suc-left
   (all num (lam n (all num (lam k (eq num (add (suc n) k) (suc (add n k)))))))
   (pf:fix n (pf:induction num-ind (lam k (eq num (add (suc n) k) (suc (add n k))))
     (pf:refl num (suc n))
     (pf:fix k (pf:assume ih
       (pf:cong num num (lam x (suc x)) (add (suc n) k) (suc (add n k)) ih))))))
!(pf:theorem &self from-at
   (all num (lam k (all num (lam n (eq num (shead num (drop k (from n))) (add n k))))))
   (pf:induction num-ind (lam k (all num (lam n (eq num (shead num (drop k (from n))) (add n k)))))
     (pf:fix n (pf:refl num n))
     (pf:fix k (pf:assume ih (pf:fix n
       (pf:trans num (shead num (drop (suc k) (from n))) (add (suc n) k) (add n (suc k))
         (pf:all-elim ih (suc n))
         (pf:all-elim (pf:all-elim (pf:known add-suc-left) n) k)))))))
; The stream from n and n in front of the stream from n + 1 agree at every
; position: a theorem, where the comparison above decides nothing.
!(pf:theorem &self from-unfolds-at
   (all num (lam k (all num (lam n
     (eq num (shead num (drop k (from n))) (shead num (drop k (scons num n (from (suc n))))))))))
   (pf:induction num-ind
     (lam k (all num (lam n
       (eq num (shead num (drop k (from n))) (shead num (drop k (scons num n (from (suc n)))))))))
     (pf:fix n (pf:refl num n))
     (pf:fix k (pf:assume ih (pf:fix n (pf:refl num (shead num (drop k (from (suc n))))))))))

; ---- Streams are their observations.  The admitted family has a law,
; Lean's extensionality of streams: two streams over one set with the same
; element at every position are equal.  A position is the head after some
; number of tails, written as the pairs reached from the two streams by
; applying the tail to both.  The law is a schema over the set; its instance
; at num is cited as (Stream-ext num).
!(set:known-proof &self Stream-ext)
!(pf:equality &self (Stream num) refl@stream subst@stream)
; By the law, the iteration from n is n in front of the iteration from
; n + 1: at every pair of positions reached, the heads agree and the tails
; are one stream.
!(pf:theorem &self iter-unfolds
   (all num (lam n (eq (Stream num) (iter num suc n) (scons num n (iter num suc (suc n))))))
   (pf:fix n
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known (Stream-ext num)) (iter num suc n))
                    (scons num n (iter num suc (suc n))))
       (pf:fix u (pf:fix v (pf:assume reach
         (pf:imp-elim
           (pf:all-elim
             (pf:imp-elim
               (pf:imp-elim
                 (pf:all-elim reach
                   (lam x (lam y (all prop (lam c
                     (imp (imp (eq num (shead num x) (shead num y))
                               (imp (eq (Stream num) (stail num x) (stail num y)) c))
                          c))))))
                 (pf:fix c (pf:assume h
                   (pf:imp-elim (pf:imp-elim h (pf:refl num n))
                                (pf:refl (Stream num) (iter num suc (suc n)))))))
               (pf:fix x (pf:fix y (pf:assume both
                 (pf:fix c (pf:assume h
                   (pf:imp-elim
                     (pf:imp-elim h
                       (pf:cong (Stream num) num (lam z (shead num z))
                         (stail num x) (stail num y)
                         (pf:imp-elim (pf:all-elim both (eq (Stream num) (stail num x) (stail num y)))
                                      (pf:assume a (pf:assume b b)))))
                     (pf:cong (Stream num) (Stream num) (lam z (stail num z))
                       (stail num x) (stail num y)
                       (pf:imp-elim (pf:all-elim both (eq (Stream num) (stail num x) (stail num y)))
                                    (pf:assume a (pf:assume b b)))))))))))
             (eq num (shead num u) (shead num v)))
           (pf:assume a (pf:assume b a)))))))))
; So from's written equation, from n = n in front of from (n + 1), has a
; solution in sets, the iteration, and is admitted on that evidence.  Its
; record names the theorem; its rule waits for an observation.
!(set:define &self from-written (-> num (Stream num))
   (by set-solution
     (pf:fix r (pf:assume k
       (pf:imp-elim (pf:all-elim k (lam n (iter num suc n))) (pf:known iter-unfolds)))))
   (= (from-written $n) (scons num $n (from-written (suc $n)))))
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self from-written))
   (quote $lean))
!(match &self (type:rule from-written $n $p $r) $n)
; It runs under observation; a stream with no observation is still not
; unfolded, and two stream terms are still not compared by conversion.
!(from-written (suc zero))
!(shead num (stail num (stail num (from-written (suc zero)))))
!(try (type:eq &self (from-written zero) (scons num zero (from-written (suc zero)))))
!(try (type:eq &self (from-written zero) (from zero)))
; Agreement at the first position is not enough: the iteration from 0 and 0
; in front of the iteration from 5 hold 0 there, and that alone does not
; give the law's premise, agreement at every position reached.
!(pf:proves &self
   (pf:imp-elim
     (pf:all-elim (pf:all-elim (pf:known (Stream-ext num)) (iter num suc zero))
                  (scons num zero (iter num suc (suc (suc (suc (suc (suc zero))))))))
     (pf:fix u (pf:fix v (pf:assume reach (pf:refl num zero)))))
   (eq (Stream num) (iter num suc zero)
                    (scons num zero (iter num suc (suc (suc (suc (suc (suc zero)))))))))

; ---- Negative controls.
; At the type of lists the same equation has no solution: the list from n
; would contain the list from n + 1 as a part, and so on.
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(try (set:define &self from-list (-> num list) (= (from-list $n) (cons $n (from-list (suc $n))))))
; The head of a number is refused on every face that meets it.
!(try (type:of &self (shead num zero)))
!(shead num zero)
; A family that is not one whose set model is known is not admitted, and
; not refused: here iteration starts after the first value.
!(try (set:family &self
   (: Stream2 (-> set set))
   (: scons2 (-> (A : set) (a : A) (s : (Stream2 A)) (Stream2 A)))
   (: shead2 (-> (A : set) (s : (Stream2 A)) A))
   (: stail2 (-> (A : set) (s : (Stream2 A)) (Stream2 A)))
   (: iter2 (-> (A : set) (f : (-> A A)) (a : A) (Stream2 A)))
   (= (shead2 $A (scons2 $A $a $s)) $a)
   (= (stail2 $A (scons2 $A $a $s)) $s)
   (= (iter2 $A $f $a) (scons2 $A ($f $a) (iter2 $A $f ($f $a))))))
; The family's names are taken.
!(try (set:family &self (: Stream (-> set set))))

; ---- Families that have no set model.  Lean proves of some families that
; no set model has them, and such a family is refuted with the theorem
; named.  The list family is admitted on its model, the finite lists of a
; set's members; its append runs by its equations.
!(set:family &self
   (: List (-> set set))
   (: lnil (-> (A : set) (List A)))
   (: lcons (-> (A : set) (a : A) (l : (List A)) (List A)))
   (: lappend (-> (A : set) (l : (List A)) (m : (List A)) (List A)))
   (= (lappend $A (lnil $A) $m) $m)
   (= (lappend $A (lcons $A $a $l) $m) (lcons $A $a (lappend $A $l $m))))
!(lappend num (lcons num zero (lnil num)) (lcons num (suc zero) (lnil num)))
; The stream equation at those lists would put a list in front of itself.
!(try (set:family &self
   (: iter-list (-> (A : set) (f : (-> A A)) (a : A) (List A)))
   (= (iter-list $A $f $a) (lcons $A $a (iter-list $A $f ($f $a))))))
; With the first value dropped, the equation is not one Lean decides: not
; admitted, and not refused.
!(try (set:family &self
   (: iter-list-next (-> (A : set) (f : (-> A A)) (a : A) (List A)))
   (= (iter-list-next $A $f $a) (lcons $A ($f $a) (iter-list-next $A $f ($f $a))))))
; A declaration at a proposition declares a proof of it.  A set with a
; proof that it is a member of itself has no set model; a set with a proof
; that the empty set is a member of it is not decided here.
!(try (set:family &self (: loop-set set) (: loop-in (In loop-set loop-set))))
!(try (set:family &self (: holder set) (: holder-in (In Empty holder))))
