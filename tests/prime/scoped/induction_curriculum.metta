(set:profile hol)

; First proofs by induction of a proof-assistant curriculum, over declared
; datatypes: the numbers and the lists of numbers.  Symmetry, transitivity
; and congruence of identity proofs are terms built from the identity
; eliminator; every induction is the recursor of the declared type at a
; family of identity types.  A definition is admitted only after the kernel
; has checked its right side against its declared type.

!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &s list (u 0) (: nil list) (: cons (-> num list list)))

; Identity proofs: congruence under the successor, symmetry, transitivity.
!(set:define &s cong-suc
   (-> (x : num) (y : num) (p : (id num x y)) (id num (suc x) (suc y)))
   (= (cong-suc $x $y $p)
      (id:eliminate num $x (lam y2 (lam e (id num (suc $x) (suc y2))))
         (refl (suc $x)) $y $p)))
!(set:define &s sym-num
   (-> (x : num) (y : num) (p : (id num x y)) (id num y x))
   (= (sym-num $x $y $p)
      (id:eliminate num $x (lam y2 (lam e (id num y2 $x))) (refl $x) $y $p)))
!(set:define &s trans-num
   (-> (x : num) (y : num) (z : num) (p : (id num x y)) (q : (id num y z))
       (id num x z))
   (= (trans-num $x $y $z $p $q)
      (id:eliminate num $y (lam z2 (lam e (id num $x z2))) $p $z $q)))

; Addition by recursion on its first argument.
!(set:define &s plus (-> num num num)
   (= (plus $m $n) (num-rec (lam t num) $n (lam k (lam r (suc r))) $m)))
!(type:eq &s (plus (suc zero) (suc zero)) (suc (suc zero)))
; 0 + n = n holds by computation: reflexivity is a proof.
!(type:check &s (lam (n : num) (refl n)) (-> (n : num) (id num (plus zero n) n)))
; n + 0 = n does not hold by computation: reflexivity is refused.
!(try (type:check &s (lam (n : num) (refl n))
        (-> (n : num) (id num (plus n zero) n))))
; n + 0 = n by induction.
!(set:define &s plus-zero
   (-> (n : num) (id num (plus n zero) n))
   (= (plus-zero $n)
      (num-rec (lam k (id num (plus k zero) k))
               (refl zero)
               (lam k (lam ih (cong-suc (plus k zero) k ih)))
               $n)))
!(type:check &s plus-zero (-> (n : num) (id num (plus n zero) n)))
; n + suc m = suc (n + m) by induction.
!(set:define &s plus-suc
   (-> (n : num) (m : num) (id num (plus n (suc m)) (suc (plus n m))))
   (= (plus-suc $n $m)
      (num-rec (lam k (id num (plus k (suc $m)) (suc (plus k $m))))
               (refl (suc $m))
               (lam k (lam ih (cong-suc (plus k (suc $m)) (suc (plus k $m)) ih)))
               $n)))
!(type:check &s plus-suc
   (-> (n : num) (m : num) (id num (plus n (suc m)) (suc (plus n m)))))
; Addition is commutative: induction with the two lemmas, symmetry and
; transitivity.
!(set:define &s plus-comm
   (-> (n : num) (m : num) (id num (plus n m) (plus m n)))
   (= (plus-comm $n $m)
      (num-rec (lam k (id num (plus k $m) (plus $m k)))
               (sym-num (plus $m zero) $m (plus-zero $m))
               (lam k (lam ih
                  (trans-num (suc (plus k $m)) (suc (plus $m k)) (plus $m (suc k))
                     (cong-suc (plus k $m) (plus $m k) ih)
                     (sym-num (plus $m (suc k)) (suc (plus $m k)) (plus-suc $m k)))))
               $n)))
!(type:check &s plus-comm (-> (n : num) (m : num) (id num (plus n m) (plus m n))))
!(type:check &s (plus-comm (suc zero) (suc (suc zero)))
   (id num (suc (suc (suc zero))) (suc (suc (suc zero)))))

; Wrong proofs of n + 0 = n are not admitted: a step without the congruence,
; and the congruence at exchanged endpoints.  A false statement, n + 0 = suc n,
; is not admitted either.
!(let $r (try (set:define &s plus-zero-without-congruence
   (-> (n : num) (id num (plus n zero) n))
   (= (plus-zero-without-congruence $n)
      (num-rec (lam k (id num (plus k zero) k)) (refl zero) (lam k (lam ih ih)) $n))))
   (car-atom $r))
!(let $r (try (set:define &s plus-zero-exchanged
   (-> (n : num) (id num (plus n zero) n))
   (= (plus-zero-exchanged $n)
      (num-rec (lam k (id num (plus k zero) k))
               (refl zero)
               (lam k (lam ih (cong-suc k (plus k zero) ih)))
               $n))))
   (car-atom $r))
!(let $r (try (set:define &s plus-zero-false
   (-> (n : num) (id num (plus n zero) (suc n)))
   (= (plus-zero-false $n)
      (num-rec (lam k (id num (plus k zero) (suc k)))
               (refl zero)
               (lam k (lam ih (cong-suc (plus k zero) (suc k) ih)))
               $n))))
   (car-atom $r))

; Lists: append, and its associativity by induction on the first list.
!(set:define &s append (-> list list list)
   (= (append $xs $ys)
      (list-rec (lam t list) $ys (lam a (lam as (lam r (cons a r)))) $xs)))
!(set:define &s cong-cons
   (-> (a : num) (x : list) (y : list) (p : (id list x y))
       (id list (cons a x) (cons a y)))
   (= (cong-cons $a $x $y $p)
      (id:eliminate list $x (lam y2 (lam e (id list (cons $a $x) (cons $a y2))))
         (refl (cons $a $x)) $y $p)))
!(set:define &s append-assoc
   (-> (xs : list) (ys : list) (zs : list)
       (id list (append (append xs ys) zs) (append xs (append ys zs))))
   (= (append-assoc $xs $ys $zs)
      (list-rec
         (lam l (id list (append (append l $ys) $zs) (append l (append $ys $zs))))
         (refl (append $ys $zs))
         (lam a (lam as (lam ih
            (cong-cons a (append (append as $ys) $zs) (append as (append $ys $zs)) ih))))
         $xs)))
!(type:check &s append-assoc
   (-> (xs : list) (ys : list) (zs : list)
       (id list (append (append xs ys) zs) (append xs (append ys zs)))))

; The length of an append is the sum of the lengths.
!(set:define &s length (-> list num)
   (= (length $l) (list-rec (lam t num) zero (lam a (lam as (lam r (suc r)))) $l)))
!(set:define &s length-append
   (-> (xs : list) (ys : list)
       (id num (length (append xs ys)) (plus (length xs) (length ys))))
   (= (length-append $xs $ys)
      (list-rec
         (lam l (id num (length (append l $ys)) (plus (length l) (length $ys))))
         (refl (length $ys))
         (lam a (lam as (lam ih
            (cong-suc (length (append as $ys)) (plus (length as) (length $ys)) ih))))
         $xs)))
!(type:check &s length-append
   (-> (xs : list) (ys : list)
       (id num (length (append xs ys)) (plus (length xs) (length ys)))))
!(type:eq &s (length (append (cons zero nil) (cons zero (cons zero nil))))
   (suc (suc (suc zero))))

; The same mathematics with functions defined by pattern equations.  The
; equations hold at open terms, so reflexivity proves an equation under
; binders.
!(set:define &s add (-> num num num)
   (= (add zero $n) $n)
   (= (add (suc $m) $n) (suc (add $m $n))))
!(type:check &s (lam (k : num) (lam (n : num) (refl (suc (add k n)))))
   (-> (k : num) (n : num) (id num (add (suc k) n) (suc (add k n)))))
; n + 0 = n as a proof by pattern equations with a recursive call: induction
; written as structural recursion.  The kernel admits it with its evidence of
; termination; at a closed number the proof computes to reflexivity.
!(set:define &s add-zero
   (-> (n : num) (id num (add n zero) n))
   (= (add-zero zero) (refl zero))
   (= (add-zero (suc $k)) (cong-suc (add $k zero) $k (add-zero $k))))
!(type:check &s add-zero (-> (n : num) (id num (add n zero) n)))
!(type:eq &s (add-zero (suc zero)) (refl (suc zero)))
; A recursive call at the same argument is no proof: the definition is not
; admitted.  It is not refuted either, since a proof does satisfy its equation.
!(let $r (try (set:define &s add-zero-loop
   (-> (n : num) (id num (add n zero) n))
   (= (add-zero-loop zero) (refl zero))
   (= (add-zero-loop (suc $k)) (add-zero-loop (suc $k)))))
   (car-atom $r))

; Case analysis: negation on the truth values is an involution.
!(set:inductive &s bool (u 0) (: true bool) (: false bool))
!(set:define &s negb (-> bool bool) (= (negb true) false) (= (negb false) true))
!(set:define &s negb-negb
   (-> (b : bool) (id bool (negb (negb b)) b))
   (= (negb-negb true) (refl true))
   (= (negb-negb false) (refl false)))
!(type:check &s negb-negb (-> (b : bool) (id bool (negb (negb b)) b)))

; Existence: a number whose double is two, as a pair of the number and the
; proof.  A wrong witness is refused.
!(set:define &s double (-> num num)
   (= (double zero) zero)
   (= (double (suc $n)) (suc (suc (double $n)))))
!(type:check &s (pair (suc zero) (refl (suc (suc zero))))
   (sigma (k : num) (id num (double k) (suc (suc zero)))))
!(type:check &s (pair zero (refl (suc (suc zero))))
   (sigma (k : num) (id num (double k) (suc (suc zero)))))
