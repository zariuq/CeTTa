; The HOL4 ladder (HOL01-HOL06): propositional logic, induction on naturals
; and lists, definitions of functions, datatypes and an inductive relation,
; higher-order predicate monotonicity, the classical theorems from an
; excluded-middle axiom, and the LCF primitives as the checker's own rules.
!(import! &self pf)
!(import! &self ../../../lib/prime/curriculum.metta)
!(bind! &thy (new-space))
!(set:inductive &thy num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &thy list (u 0) (: nil list) (: cons (-> num list list)))
!(pf:equality &thy num refl@num subst@num)
!(pf:equality &thy list refl@list subst@list)
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy neg (-> prop prop) (= (neg $p) (imp $p falsum)))
!(set:define &thy and (-> prop prop prop) (= (and $p $q) (all prop (lam r (imp (imp $p (imp $q r)) r)))))
!(set:define &thy or (-> prop prop prop) (= (or $p $q) (all prop (lam r (imp (imp $p r) (imp (imp $q r) r))))))

; ---- HOL01 logic
!(pf:theorem &thy imp-id (all prop (lam p (imp p p))) (pf:fix p (pf:assume h h)))
!(pf:theorem &thy and-comm (all prop (lam p (all prop (lam q (imp (and p q) (and q p))))))
   (pf:fix p (pf:fix q (pf:assume h (pf:fix r (pf:assume k (pf:by h (pf:assume hp (pf:assume hq (pf:by k hq hp))))))))))
!(pf:theorem &thy or-comm (all prop (lam p (all prop (lam q (imp (or p q) (or q p))))))
   (pf:fix p (pf:fix q (pf:assume h (pf:fix r (pf:assume kq (pf:assume kp (pf:by h kp kq))))))))
; de Morgan, the constructive half: neg p or neg q implies neg (p and q)
!(pf:theorem &thy demorgan-half (all prop (lam p (all prop (lam q (imp (or (neg p) (neg q)) (neg (and p q)))))))
   (pf:fix p (pf:fix q (pf:assume h (pf:assume hpq
     (pf:by h (pf:assume np (pf:by np (pf:by hpq (pf:assume hp (pf:assume hq hp)))))
              (pf:assume nq (pf:by nq (pf:by hpq (pf:assume hp (pf:assume hq hq)))))))))))

; ---- HOL02 induction
!(set:define &thy add (-> num num num) (= (add zero $m) $m) (= (add (suc $n) $m) (suc (add $n $m))))
!(pf:theorem &thy add-zero-r (all num (lam n (eq num (add n zero) n)))
   (pf:induction num-ind (lam n (eq num (add n zero) n))
     (pf:refl num zero)
     (pf:fix k (pf:assume ih (pf:cong num num (lam r (suc r)) (add k zero) k ih)))))
!(pf:theorem &thy add-suc-r (all num (lam n (all num (lam m (eq num (add n (suc m)) (suc (add n m)))))))
   (pf:induction num-ind (lam n (all num (lam m (eq num (add n (suc m)) (suc (add n m))))))
     (pf:fix m (pf:refl num (suc m)))
     (pf:fix k (pf:assume ih (pf:fix m (pf:cong num num (lam r (suc r)) (add k (suc m)) (suc (add k m)) (pf:by ih)))))))
!(pf:theorem &thy add-comm-ind (all num (lam n (all num (lam m (eq num (add n m) (add m n))))))
   (pf:induction num-ind (lam n (all num (lam m (eq num (add n m) (add m n)))))
     (pf:fix m (pf:sym num (add m zero) m (pf:by add-zero-r)))
     (pf:fix k (pf:assume ih (pf:fix m
       (pf:calc num (add (suc k) m)
         (= (suc (add m k))  (cong num (lam r (suc r)) (add k m) (add m k) (pf:by ih)))
         (= (add m (suc k))  (sym (by add-suc-r)))))))))
!(set:define &thy append (-> list list list) (= (append nil $k) $k) (= (append (cons $x $l) $k) (cons $x (append $l $k))))
!(set:define &thy length (-> list num) (= (length nil) zero) (= (length (cons $x $l)) (suc (length $l))))
!(set:define &thy rev (-> list list) (= (rev nil) nil) (= (rev (cons $x $l)) (append (rev $l) (cons $x nil))))
!(pf:theorem &thy length-append (all list (lam l (all list (lam k (eq num (length (append l k)) (add (length l) (length k)))))))
   (pf:induction list-ind (lam l (all list (lam k (eq num (length (append l k)) (add (length l) (length k))))))
     (pf:fix k (pf:refl num (length k)))
     (pf:fix x (pf:fix l (pf:assume ih (pf:fix k (pf:cong num num (lam r (suc r)) (length (append l k)) (add (length l) (length k)) (pf:by ih))))))))
!(pf:theorem &thy append-nil (all list (lam l (eq list (append l nil) l)))
   (pf:induction list-ind (lam l (eq list (append l nil) l))
     (pf:refl list nil)
     (pf:fix x (pf:fix l (pf:assume ih (pf:cong list list (lam r (cons x r)) (append l nil) l ih))))))
!(pf:theorem &thy append-assoc (all list (lam l (all list (lam k (all list (lam m (eq list (append (append l k) m) (append l (append k m)))))))))
   (pf:induction list-ind (lam l (all list (lam k (all list (lam m (eq list (append (append l k) m) (append l (append k m))))))))
     (pf:fix k (pf:fix m (pf:refl list (append k m))))
     (pf:fix x (pf:fix l (pf:assume ih (pf:fix k (pf:fix m (pf:cong list list (lam r (cons x r)) (append (append l k) m) (append l (append k m)) (pf:by ih)))))))))
!(pf:theorem &thy rev-append (all list (lam l (all list (lam k (eq list (rev (append l k)) (append (rev k) (rev l)))))))
   (pf:induction list-ind (lam l (all list (lam k (eq list (rev (append l k)) (append (rev k) (rev l))))))
     (pf:fix k (pf:sym list (append (rev k) nil) (rev k) (pf:by append-nil)))
     (pf:fix x (pf:fix l (pf:assume ih (pf:fix k
       (pf:calc list (rev (append (cons x l) k))
         (= (append (append (rev k) (rev l)) (cons x nil))  (cong list (lam r (append r (cons x nil))) (rev (append l k)) (append (rev k) (rev l)) (pf:by ih)))
         (= (append (rev k) (append (rev l) (cons x nil)))  (by append-assoc)))))))))
!(pf:theorem &thy rev-rev (all list (lam l (eq list (rev (rev l)) l)))
   (pf:induction list-ind (lam l (eq list (rev (rev l)) l))
     (pf:refl list nil)
     (pf:fix x (pf:fix l (pf:assume ih
       (pf:calc list (rev (rev (cons x l)))
         (= (append (rev (cons x nil)) (rev (rev l)))  (by rev-append))
         (= (cons x l)                                 (cong list (lam r (cons x r)) (rev (rev l)) l (pf:by ih)))))))))

; ---- HOL03 definitions: a function, a datatype with its size, an inductive relation
!(set:define &thy dbl (-> num num) (= (dbl zero) zero) (= (dbl (suc $n)) (suc (suc (dbl $n)))))
!(pf:theorem &thy dbl-two (eq num (dbl (suc (suc zero))) (suc (suc (suc (suc zero))))) (pf:refl num (suc (suc (suc (suc zero))))))
!(set:inductive &thy tree (u 0) (: leaf tree) (: node (-> tree num tree tree)))
!(set:define &thy tsize (-> tree num) (= (tsize leaf) zero) (= (tsize (node $l $v $r)) (suc (add (tsize $l) (tsize $r)))))
!(pf:theorem &thy tsize-node (eq num (tsize (node leaf zero leaf)) (suc zero)) (pf:refl num (suc zero)))
!(set:define &thy ev (-> num prop)
   (= (ev $n) (all (-> num prop) (lam P (imp (P zero) (imp (all num (lam m (imp (P m) (P (suc (suc m)))))) (P $n)))))))
!(pf:theorem &thy ev-ss0 (ev (suc (suc zero)))
   (pf:fix P (pf:assume base (pf:assume step (pf:imp-elim (pf:all-elim step zero) base)))))

; ---- HOL04 higher order: predicate monotonicity, and choice as a declared operator
!(pf:theorem &thy pred-mono
   (all (-> num prop) (lam P (all (-> num prop) (lam Q (imp (all num (lam x (imp (P x) (Q x)))) (imp (all num (lam x (P x))) (all num (lam x (Q x)))))))))
   (pf:fix P (pf:fix Q (pf:assume mono (pf:assume allp (pf:fix x (pf:by mono (pf:all-elim allp x))))))))
!(pf:theory &thy ((: choose (-> (-> num prop) num))))
!(set:axiom &thy select-intro (all (-> num prop) (lam P (all num (lam x (imp (P x) (P (choose P))))))))
!(pf:theorem &thy select-use (all (-> num prop) (lam P (imp (P zero) (P (choose P)))))
   (pf:fix P (pf:assume h (pf:by select-intro h))))

; ---- HOL05 classical: from excluded middle, double negation and Peirce
!(set:axiom &thy excluded-middle (all prop (lam p (or p (neg p)))))
!(pf:theorem &thy double-neg (all prop (lam p (imp (neg (neg p)) p)))
   (pf:fix p (pf:assume nnp
     (pf:by (pf:all-elim (pf:known excluded-middle) p) (pf:assume hp hp) (pf:assume np (pf:by (pf:imp-elim nnp np)))))))
!(pf:theorem &thy peirce (all prop (lam p (all prop (lam q (imp (imp (imp p q) p) p)))))
   (pf:fix p (pf:fix q (pf:assume h
     (pf:by (pf:all-elim (pf:known excluded-middle) p) (pf:assume hp hp) (pf:assume np (pf:by h (pf:assume hp (pf:by (pf:imp-elim np hp))))))))))

; ---- HOL06 the LCF primitives are the checker's rules: REFL, ASSUME/DISCH, BETA_CONV
!(pf:theory &thy ((: x num) (: y num)))
!(pf:proves &thy (pf:refl num x) (eq num x x))
!(pf:proves &thy (pf:fix p (pf:assume hp hp)) (all prop (lam p (imp p p))))
!(set:define &thy apply (-> (-> num num) num num) (= (apply $f $x) ($f $x)))
!(pf:proves &thy (pf:refl num y) (eq num (apply (lam z z) y) y))
