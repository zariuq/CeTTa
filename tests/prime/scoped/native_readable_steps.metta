(set:profile hol)

; Readable steps compile natively, in the hol profile as in any other.  A
; local lemma (pf:have) is a redex: the lemma's proof given to a function of
; its name, so the lemma is checked once and each use is a variable.  A pf:by
; step is the eliminations the checker found for it: the fact applied to a
; witness for each quantifier its prefix takes and to the given proof of each
; antecedent.  The package's proof is the proof as compiled, written in the
; rules of the logic, and the checker accepts it in its own right.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &thy ((: P (-> set prop)) (: Q (-> set prop)) (: R (-> set prop)) (: c set)
                  (: All (-> (-> set prop) prop))))
!(set:axiom &thy p-to-q (all set (lam x (imp (P x) (Q x)))))
!(set:axiom &thy q-to-r (all set (lam x (imp (Q x) (R x)))))
!(set:axiom &thy p-c (P c))
!(set:axiom &thy all-intro (all (-> set prop) (lam F (imp (all set (lam z (F z))) (All F)))))

; A stated lemma proved by pf:by, then pf:by with the lemma as its premise.
!(pf:theorem &thy r-c (R c)
   (pf:have q-c (Q c) (pf:by p-to-q (pf:known p-c))
     (pf:by q-to-r q-c)))
; A lemma whose statement is read off its proof, under binders of the proof.
!(pf:theorem &thy p-to-r (all set (lam y (imp (P y) (R y))))
   (pf:fix y (pf:assume h
     (pf:have step (pf:imp-elim (pf:all-elim (pf:known p-to-q) y) h)
       (pf:by q-to-r step)))))
; A witness that is a function, found by matching: it mentions the binder x
; by name, and its own binder takes a name the proof does not use.
!(pf:theorem &thy all-keeps (all set (lam x (All (lam z (imp (P x) (imp (P z) (P x)))))))
   (pf:fix x (pf:by all-intro (pf:fix z (pf:assume hx (pf:assume hz hx))))))
; The fact of a pf:by step may be a hypothesis or a proof term.
!(pf:theorem &thy q-c-again (all set (lam y (imp (all set (lam x (imp (P x) (Q x)))) (Q c))))
   (pf:fix y (pf:assume h (pf:by h (pf:known p-c)))))
!(pf:theorem &thy r-c-again (R c)
   (pf:by (pf:all-elim (pf:known q-to-r) c) (pf:by p-to-q (pf:known p-c))))

; Each has a package.  Its proof is the proof as compiled: no pf:by is left,
; the lemmas stay, and the checker accepts that proof of the statement.
(= (mentions $t $s)
   (if (== $t $s) True
       (if (== (get-metatype $t) Expression) (mentions-any $t $s) False)))
(: mentions-any (-> (Data Atom) Atom Bool))
(= (mentions-any $t $s)
   (if (== $t ()) False
       (let $h (car-atom $t)
         (let $rest (cdr-atom $t)
           (if (mentions $h $s) True (mentions-any $rest $s))){$rest}){$h}))
(= (compiled $name)
   (let (quote (SetNativeProofV1 $n $prop $proof $term $type $context $rules $rows $digest))
        (lift (set:native-proof &thy $name))
     (Compiled $name $proof (mentions $proof pf:by) (mentions $proof pf:have)
               (set:proves &thy (quote $proof) (quote $prop)))))
!(compiled r-c)
!(compiled p-to-r)
!(compiled all-keeps)
!(compiled q-c-again)
!(compiled r-c-again)

; The lemma of r-c is a redex in its term: a function of the lemma's name
; applied to the lemma's proof.
!(let (SetNativeProofV1 $n $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy r-c)
   (unify $term (App (Lam $domain $body) $lemma) LemmaIsARedex NotARedex))

; Read as data, a lemma is its proof: a consumer of the proof that uses a
; lemma once computes to what it computes to on the proof without it.
!(pf:theorem &thy self-imp (all prop (lam p (imp p p))) (pf:fix p (pf:assume h h)))
!(pf:theorem &thy self-imp-lemma (all prop (lam p (imp p p)))
   (pf:fix p (pf:assume h (pf:have k h k))))
!(let $with (set:native-proof &thy self-imp-lemma)
 (let $without (set:native-proof &thy self-imp)
  (unify $with (SetNativeProofV1 $n $prop $proof $term $type $context $rules $rows $digest)
   (let $consumer (Lam $type (App (idx 0) (DeclConst Falsum)))
     (let (SetNativeNormalFormV1 $c1 $b1 $application $normal $s1 $r1)
          (set:native-normalize &thy $with $consumer)
       (let (SetNativeNormalFormV1 $c2 $b2 $a2 $normal2 $s2 $r2)
            (set:native-normalize &thy $without $consumer)
         (SameValue (== $normal $normal2) (== $r1 $r2))){$c2 $b2 $a2 $normal2 $s2 $r2}){$c1 $b1 $application $normal $s1 $r1}){$consumer}
   NotAPackage)))

; A step that proves nothing publishes nothing, so nothing has a package of
; it: the fact does not give the goal, or the lemma does not hold.
!(let ($verdict $why) (try (set:theorem &thy wrong-by (quote (R c)) (quote (pf:by p-to-q (pf:known p-c)))))
   (WrongBy $verdict))
!(let ($verdict $why) (try (set:theorem &thy wrong-lemma (quote (R c))
                                (quote (pf:have r (R c) (pf:known p-c) r))))
   (WrongLemma $verdict))
!(let $r (try (set:native-proof &thy wrong-by)) (car-atom $r))
