; Conversion compares terms at their types. At a function type both sides are
; applied to a fresh variable, at a pair type their projections are compared,
; so eta holds for functions and pairs while equations still fire only at
; their authored arity. The identity eliminator computes on reflexivity by
; its declared rule; there is no definitional uniqueness of identity proofs.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(set:define &self eqAt (-> num (u 0))
   (= (eqAt $n) (id num (add zero $n) $n)))
!(set:define &self keepCert
   (-> (A : (u 0)) (P : (-> A (u 0))) (x : A) (e : (P x))
       (sigma (y : A) (P y)))
   (= (keepCert $A $P $x $e) (pair $x $e)))
!(add-atom &self (: h (-> num num)))
!(add-atom &self (: q (sigma (y : num) (eqAt y))))
!(add-atom &self (: q2 (sigma (y : num) (eqAt y))))
!(add-atom &self (: k (-> (e : (eqAt zero)) (sigma (y : num) (eqAt y)))))

; Eta for functions and for pairs, at neutral terms.
!(let $r (try (type:check &self (refl h) (id (-> num num) h (lam x (h x))))) $r)
!(let $r (try (type:check &self (refl q)
   (id (sigma (y : num) (eqAt y)) q (pair (fst q) (snd q))))) $r)
; A different pair is not equal to q.
!(let $r (try (type:check &self (refl q)
   (id (sigma (y : num) (eqAt y)) q (pair (fst q2) (snd q2))))) $r)

; A partial application of an arity-4 equation meets its expansion: the
; fresh argument completes the call, which then fires. Only the verdict is
; shown; the checked type holds the partial application itself.
!(let $r (try (type:check &self (refl (keepCert num eqAt zero))
   (id (-> (e : (eqAt zero)) (sigma (y : num) (eqAt y)))
       (keepCert num eqAt zero) (lam e (pair zero e))))) (car-atom $r))
; It does not meet an unrelated function of the same type.
!(let $r (try (type:check &self (refl (keepCert num eqAt zero))
   (id (-> (e : (eqAt zero)) (sigma (y : num) (eqAt y)))
       (keepCert num eqAt zero) k))) $r)

; Eliminating reflexivity along an eta expansion computes to the method,
; and the unfolded eliminator is typed at the call's type.
!(set:define &self alongEta
   (-> (h : (-> (e : (eqAt zero)) (sigma (y : num) (eqAt y)))) num)
   (= (alongEta $h)
      (id:eliminate (-> (e : (eqAt zero)) (sigma (y : num) (eqAt y))) $h
         (lam y (lam p num)) (suc zero) (lam e ($h e)) (refl $h))))
!(let $r (try (type:eq &self (alongEta (keepCert num eqAt zero)) (suc zero))) $r)
!(let $r (try (type:eq &self (alongEta (keepCert num eqAt zero)) zero)) $r)
!(let $r (try (type:check &self (id:eliminate (-> (e : (eqAt zero)) (sigma (y : num) (eqAt y))) (keepCert num eqAt zero) (lam y (lam p num)) (suc zero) (lam e (keepCert num eqAt zero e)) (refl (keepCert num eqAt zero))) num)) (car-atom $r))

; The declared eliminator computes at reflexivity and is stuck at a neutral
; path. A neutral path is not reflexivity.
!(add-atom &self (: path (id num zero zero)))
!(let $r (try (type:eq &self (id:eliminate num zero (lam y (lam p num)) (suc zero) zero (refl zero)) (suc zero))) $r)
!(let $r (try (type:eq &self (id:eliminate num zero (lam y (lam p num)) (suc zero) zero path) (suc zero))) $r)
!(let $r (try (type:eq &self path (refl zero))) $r)
; An ill-typed elimination is not typed, so it is never computed.
!(let $r (try (type:check &self (id:eliminate num zero (lam y (lam p num)) (suc zero) (suc zero) (refl zero)) num)) $r)
; The eliminator is language-owned and cannot be redefined.
!(let $r (try (set:define &self id:eliminate (-> num num) (= (id:eliminate $n) $n))) $r)

; Ordinary evaluation of a raw call keeps comparing the endpoints: an
; ill-typed elimination stays as it is, a well-typed one computes.
!(id:eliminate num zero (lam y (lam p num)) (suc zero) (suc zero) (refl zero))
!(id:eliminate num zero (lam y (lam p num)) (suc zero) zero (refl zero))
; Quoted terms are syntax: eta does not relate them.
!(== (quote h) (quote (lam x (h x))))
