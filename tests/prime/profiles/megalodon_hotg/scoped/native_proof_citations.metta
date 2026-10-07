(set:profile megalodon-hotg)

; Native proof terms cite theorems by name.  A theorem published in a theory
; is a constant of the package of every proof that cites it: the package
; declares the constant at the theorem's statement, the proof term mentions
; it, and the cited proof, checked where the theorem was published, is not
; compiled again.  A theorem is cited only while it is current where the
; package is built: published there and not withdrawn, with everything it
; was checked against unchanged and available.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))

; Two declared sets and one assumption about them, not provable from the
; laws: every member of c is a member of d.
!(pf:theory &thy ((: c set) (: d set)))
!(set:axiom &thy c-in-d (all set (lam x (imp (In x c) (In x d)))))
; A lemma from the assumption, and a theorem that cites the lemma twice.
!(pf:theorem &thy c-in-d-again (all set (lam y (imp (In y c) (In y d))))
   (pf:fix y (pf:assume h (pf:imp-elim (pf:all-elim (pf:known c-in-d) y) h))))
!(pf:theorem &thy two-in-d
   (all set (lam x (all set (lam y (imp (In x c) (imp (In y c) (all prop (lam p
     (imp (imp (In x d) (imp (In y d) p)) p)))))))))
   (pf:fix x (pf:fix y (pf:assume hx (pf:assume hy (pf:fix p (pf:assume k
     (pf:imp-elim
       (pf:imp-elim k (pf:imp-elim (pf:all-elim (pf:known c-in-d-again) x) hx))
       (pf:imp-elim (pf:all-elim (pf:known c-in-d-again) y) hy)))))))))

; The package of two-in-d declares the lemma once, by name; the assumption
; the lemma uses is reached through it.  The constant's type is the type of
; the lemma's own package: the lemma's package discharges it.
(= (declared (PrimeCtxDecl (DeclConst $k) $t $rest) $c)
   (if (== $k $c) $t (declared $rest $c)))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy two-in-d)
   (let (($cited $statement $constant)) $rows
     (let (SetNativeProofV1 $n2 $p2 $f2 $t2 $lemma-type $c2 $r2 $rows2 $d2)
          (set:native-proof &thy c-in-d-again)
       (Cites $name $cited (== (declared $context $constant) $lemma-type)
              (map-atom $rows2 $r (car-atom $r))))))
!(pf:rests-on &thy two-in-d)

; The hypothetical judgment: the lemma was checked under the assumption.
; While the assumption is withdrawn the lemma is not cited, and the package of
; two-in-d is not built; published again, it is cited again.
!(let $removed (match &thy (set:known c-in-d $p $o $f $d $r $s)
                 (remove-atom &thy (set:known c-in-d $p $o $f $d $r $s)))
   (let $result (try (set:native-proof &thy two-in-d)) (AssumptionWithdrawn $result)))
!(set:axiom &thy c-in-d (all set (lam x (imp (In x c) (In x d)))))
!(let $result (try (set:native-proof &thy two-in-d)) (AssumptionRestored (car-atom $result)))

; A package saved now, before the lemma is withdrawn.
!(bind! &saved (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(let $package (set:native-proof &thy two-in-d) (add-atom &saved (saved $package)))

; Withdrawal: under another statement of the assumption the lemma's recheck
; fails, which withdraws its admission.  The theorem that cites it has no
; package, and the saved package is not used, until the lemma is checked
; again.
!(let $removed (match &thy (set:known c-in-d $p $o $f $d $r $s)
                 (remove-atom &thy (set:known c-in-d $p $o $f $d $r $s)))
   (set:axiom &thy c-in-d (all set (lam x (imp (In x d) (In x c))))))
!(set:recheck &thy c-in-d-again)
!(let $removed (match &thy (set:known c-in-d $p $o $f $d $r $s)
                 (remove-atom &thy (set:known c-in-d $p $o $f $d $r $s)))
   (set:axiom &thy c-in-d (all set (lam x (imp (In x c) (In x d))))))
!(let $result (try (set:native-proof &thy two-in-d)) (LemmaWithdrawn $result))
!(let ($verdict $why)
      (try (set:proves &thy (quote (pf:known c-in-d-again))
                            (quote (all set (lam y (imp (In y c) (In y d)))))))
   (LemmaNotCited $verdict $why))
!(match &saved (saved $package)
   (unify $package (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
     (let $result (try (set:native-use &thy $package (Lam $type (idx 0))))
       (SavedPackageAfterWithdrawal $result))
     NotAPackage))
!(set:recheck &thy c-in-d-again)
!(let $result (try (set:native-proof &thy two-in-d)) (LemmaCheckedAgain (car-atom $result)))
!(match &saved (saved $package)
   (unify $package (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
     (let (SetNativeUseV1 $checked $consumer $application $result-type)
          (set:native-use &thy $package (Lam $type (idx 0)))
       (SavedPackageUsed (== $checked $package)))
     NotAPackage))

; A name that was never published is not cited: a record inserted into the
; space with a statement the laws refute is not admitted, so neither a proof
; nor a package can cite it.
!(let $d (set:signature-digest)
   (add-atom &thy (set:known forged-lemma (In Empty Empty) theorem (pf:known c-in-d) () 0 $d)))
!(let ($verdict $why)
      (try (set:proves &thy (quote (pf:known forged-lemma)) (quote (In Empty Empty))))
   ($verdict $why))
!(try (set:native-proof &thy forged-lemma))

; A theorem published with a readable proof has a package of its own: its
; pf:by step is compiled to the eliminations the checker found for it, here
; none, since the assumption is the statement itself, and the package's proof
; is the proof as compiled.  It is cited by name as any theorem is, and what
; rests on it is read as data through it.
!(pf:theorem &thy c-in-d-by (all set (lam y (imp (In y c) (In y d)))) (pf:by c-in-d))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy c-in-d-by)
   (ReadableCompiled $proof (map-atom $rows $r (car-atom $r))))
!(pf:theorem &thy cites-readable (all set (lam y (imp (In y c) (In y d)))) (pf:known c-in-d-by))
!(let $result (try (set:native-proof &thy cites-readable)) (CitesReadable (car-atom $result)))
!(let $package (set:native-proof &thy cites-readable)
   (let (SetNativeLinkedV1 $interpretation $checked $linked $type $context $rules $rows)
        (set:native-link &thy identity $package)
     (ReadAsData (map-atom $rows $r (let $kind (car-atom $r) (let $law (index-atom $r 1) ($kind $law)))))))
!(collapse (pf:rests-on &thy cites-readable))

; Nor is a theorem of another theory, though it declares the same sets.
!(bind! &other (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(pf:theory &other ((: c set) (: d set)))
!(let ($verdict $why)
      (try (set:proves &other (quote (pf:known c-in-d-again))
                              (quote (all set (lam y (imp (In y c) (In y d)))))))
   ($verdict $why))

; Reading a proof as data goes through what it cites: computing a consumer's
; value and linking under the identity interpretation compile each cited
; proof in place of its name.  A theorem whose proof is a citation computes
; to what the cited theorem computes to; the application keeps the citation.
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
           (== $application (App $consumer $term)) (== $normal $normal1) (== $result $result1)))))
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
