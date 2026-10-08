(set:profile hol)

; Logical theorems cite each other by name in the hol profile, as set
; theorems do in megalodon-hotg.  A theorem published in a theory is a
; constant of the package of every proof that cites it: the package declares
; the constant at the theorem's statement, the proof term mentions it, and the
; cited proof, checked where the theorem was published, is not compiled again.
; A theorem is cited only while it is current where the package is built:
; published there and not withdrawn, under the same profile, with everything
; it was checked against unchanged and available.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))

; A declared predicate pair and one assumption about them, not provable in
; the logic: whatever is P is Q.
!(pf:theory &thy ((: P (-> set prop)) (: Q (-> set prop)) (: c set)))
!(set:axiom &thy p-to-q (all set (lam x (imp (P x) (Q x)))))
; A lemma from the assumption, and a theorem that cites the lemma twice.
!(pf:theorem &thy p-to-q-again (all set (lam y (imp (P y) (Q y))))
   (pf:fix y (pf:assume h (pf:imp-elim (pf:all-elim (pf:known p-to-q) y) h))))
!(pf:theorem &thy both-q
   (all set (lam x (all set (lam y (imp (P x) (imp (P y) (all prop (lam r
     (imp (imp (Q x) (imp (Q y) r)) r)))))))))
   (pf:fix x (pf:fix y (pf:assume hx (pf:assume hy (pf:fix r (pf:assume k
     (pf:imp-elim
       (pf:imp-elim k (pf:imp-elim (pf:all-elim (pf:known p-to-q-again) x) hx))
       (pf:imp-elim (pf:all-elim (pf:known p-to-q-again) y) hy)))))))))

; The package of both-q declares the lemma once, by name; the assumption the
; lemma uses is reached through it.  The constant's type is the type of the
; lemma's own package.  The packages carry the hol signature's digest.
(= (declared (PrimeCtxDecl (DeclConst $k) $t $rest) $c)
   (if (== $k $c) $t (declared $rest $c)))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy both-q)
   (let (($cited $statement $constant)) $rows
     (let (SetNativeProofV1 $n2 $p2 $f2 $t2 $lemma-type $c2 $r2 $rows2 $d2)
          (set:native-proof &thy p-to-q-again)
       (Cites $name $cited (== (declared $context $constant) $lemma-type)
              (map-atom $rows2 $r (car-atom $r))
              (== $digest (set:signature-digest &thy))))))
!(pf:rests-on &thy both-q)

; The hypothetical judgment: the lemma was checked under the assumption.
; While the assumption is withdrawn the lemma is not cited, and the package of
; both-q is not built; published again, it is cited again.
!(let $removed (match &thy (set:known p-to-q $p $o $f $d $r $s)
                 (remove-atom &thy (set:known p-to-q $p $o $f $d $r $s)))
   (let $result (try (set:native-proof &thy both-q)) (AssumptionWithdrawn $result)))
!(set:axiom &thy p-to-q (all set (lam x (imp (P x) (Q x)))))
!(let $result (try (set:native-proof &thy both-q)) (AssumptionRestored (car-atom $result)))

; A package saved now, before the lemma is withdrawn.
!(bind! &saved (new-space))
!(let $package (set:native-proof &thy both-q) (add-atom &saved (saved $package)))

; Withdrawal: under the converse assumption the lemma's recheck fails, which
; withdraws its admission.  The theorem that cites it has no package, and the
; saved package is not used, until the lemma is checked again.
!(let $removed (match &thy (set:known p-to-q $p $o $f $d $r $s)
                 (remove-atom &thy (set:known p-to-q $p $o $f $d $r $s)))
   (set:axiom &thy p-to-q (all set (lam x (imp (Q x) (P x))))))
!(set:recheck &thy p-to-q-again)
!(let $removed (match &thy (set:known p-to-q $p $o $f $d $r $s)
                 (remove-atom &thy (set:known p-to-q $p $o $f $d $r $s)))
   (set:axiom &thy p-to-q (all set (lam x (imp (P x) (Q x))))))
!(let $result (try (set:native-proof &thy both-q)) (LemmaWithdrawn $result))
!(let ($verdict $why)
      (try (set:proves &thy (quote (pf:known p-to-q-again))
                            (quote (all set (lam y (imp (P y) (Q y)))))))
   (LemmaNotCited $verdict $why))
!(match &saved (saved $package)
   (unify $package (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
     (let $result (try (set:native-use &thy $package (Lam $type (idx 0))))
       (SavedPackageAfterWithdrawal $result)){$result}
     NotAPackage))
!(set:recheck &thy p-to-q-again)
!(let $result (try (set:native-proof &thy both-q)) (LemmaCheckedAgain (car-atom $result)))
!(match &saved (saved $package)
   (unify $package (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
     (let (SetNativeUseV1 $checked $consumer $application $result-type)
          (set:native-use &thy $package (Lam $type (idx 0)))
       (SavedPackageUsed (== $checked $package))){$checked $consumer $application $result-type}
     NotAPackage))

; A name that was never published is not cited: a record inserted into the
; space is not admitted, so neither a proof nor a package can cite it.
!(let $d (set:signature-digest &thy)
   (add-atom &thy (set:known forged-lemma (all set (lam x (Q x))) theorem
                             (pf:known p-to-q) () 0 $d)))
!(let ($verdict $why)
      (try (set:proves &thy (quote (pf:known forged-lemma)) (quote (all set (lam x (Q x))))))
   ($verdict $why))
!(try (set:native-proof &thy forged-lemma))

; Nor is a theorem of another theory, though it declares the same names.
!(bind! &other (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &other ((: P (-> set prop)) (: Q (-> set prop)) (: c set)))
!(let ($verdict $why)
      (try (set:proves &other (quote (pf:known p-to-q-again))
                              (quote (all set (lam y (imp (P y) (Q y)))))))
   ($verdict $why))

; Reading a proof as data goes through what it cites: computing a consumer's
; value compiles each cited proof in place of its name.  A theorem whose proof
; is a citation computes to what the cited theorem computes to; the
; application keeps the citation.
!(pf:theorem &thy identity (all prop (lam p (imp p p))) (pf:fix p (pf:assume h h)))
!(pf:theorem &thy identity-again (all prop (lam p (imp p p))) (pf:known identity))
!(let $copy (set:native-proof &thy identity-again)
 (let $original (set:native-proof &thy identity)
  (unify $copy (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
   (let $consumer (Lam $type (App (idx 0) (DeclConst Falsum)))
     (let (SetNativeNormalFormV1 $checked $body $application $normal $source $result)
          (set:native-normalize &thy $copy $consumer)
       (let (SetNativeNormalFormV1 $checked1 $body1 $application1 $normal1 $source1 $result1)
            (set:native-normalize &thy $original $consumer)
         (ComputedThroughCitation (map-atom $rows $r (car-atom $r))
           (== $application (App $consumer $term)) (== $normal $normal1) (== $result $result1))){$checked1 $body1 $application1 $normal1 $source1 $result1}){$checked $body $application $normal $source $result}){$consumer}
   NotAPackage)))

; zero-at-two cites zero-add by name; its link realizes the assumptions
; zero-add rests on, and the linked proof at two computes to reflexivity.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(pf:equality &self num refl@num subst@num)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(pf:theorem &self zero-add (all num (lam k (eq num (add zero k) k)))
   (pf:induction num-ind (lam k (eq num (add zero k) k))
     (pf:refl num zero)
     (pf:fix k (pf:assume ih (pf:cong num num (lam r (suc r)) (add zero k) k ih)))))
!(pf:theorem &self zero-at-two
   (eq num (add zero (suc (suc zero))) (suc (suc zero)))
   (pf:all-elim (pf:known zero-add) (suc (suc zero))))
!(let $package (set:native-proof zero-at-two)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest) $package
     (let (SetNativeLinkedV1 $interpretation $checked $linked $linked-type $linked-context
                             $linked-rules $realizations)
          (set:native-link identity $package)
       (Linked (map-atom $rows $r (car-atom $r)) (== $checked $package)
               (map-atom $realizations $r
                 (let $kind (car-atom $r) (let $law (index-atom $r 1) ($kind $law))))))))
!(let $package (set:native-proof zero-at-two)
   (let $linked (set:native-link identity $package)
     (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
       (let (SetNativeNormalFormV1 $used $consumer $application $normal $original $result)
            (set:native-normalize $linked (Lam $type (idx 0)))
         (LinkedValue $normal)))))
