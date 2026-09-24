; Linking a native proof package under the named identity interpretation.
; The retained source proof of zero-add uses three assumptions: induction,
; reflexivity and substitution. Linking replaces each by a realization that
; the kernel checks: the recursor, reflexivity, and identity elimination.
; Under the identity interpretation a represented equation reads as an
; identity type. The linked proof is then identity evidence at an open index,
; it feeds the successor step there, and its closed instances compute.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(pf:equality &self num refl@num subst@num)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(set:define &self eqAt (-> num (u 0))
   (= (eqAt $n) (id num (add zero $n) $n)))
!(set:define &self sucMove
   (-> (n : num) (e : (eqAt n)) (eqAt (suc n)))
   (= (sucMove $n $e)
      (id:eliminate num (add zero $n)
         (lam y (lam p (id num (suc (add zero $n)) (suc y))))
         (refl (suc (add zero $n)))
         $n $e)))
!(import! &self ./certified_transform_lib.metta)
!(set:define &self sucStep
   (-> (x : num) (e : (eqAt x)) (sigma (y : num) (eqAt y)))
   (= (sucStep $x $e) (transportCert num eqAt suc sucMove $x $e)))
!(pf:theorem &self zero-add (all num (lam k (eq num (add zero k) k)))
   (pf:induction num-ind (lam k (eq num (add zero k) k))
     (pf:refl num zero)
     (pf:fix k (pf:assume ih (pf:cong num num (lam r (suc r)) (add zero k) k ih)))))


; The realizations, by assumption. The linked context declares identity
; elimination and the recursor, and no assumption.
!(let $package (set:native-proof zero-add)
  (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows)
       (set:native-link identity $package)
   (let ((Realized $ind $c1 $r1) (Realized $refl $c2 $r2) (Realized $subst $c3 $r3)) $rows
    (let (PrimeCtxDecl $k1 $t1 (PrimeCtxDecl $k2 $t2 $rest)) $context
     (Linked $interpretation (== $checked $package) $ind $refl $r2 $subst $k1 $k2)))))

; The linked proof's checked type, read through the identity interpretation.
!(let $package (set:native-proof zero-add)
  (let $linked (set:native-link identity $package)
   (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
    (let (SetNativeUseV1 $used $consumer $application $result-type)
         (set:native-use $linked (Lam $type (idx 0)))
     (LinkedType $result-type)))))

; An open-index consumer that needs the source-derived witness.
!(let $package (set:native-proof zero-add)
  (let $linked (set:native-link identity $package)
   (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
    (let (SetNativeUseV1 $used $consumer $application $result-type)
         (set:native-use $linked
           (Lam $type (Lam (DeclConst num)
             (App (App (DeclConst sucStep) (idx 0)) (App (idx 1) (idx 0))))))
     (OpenConsumer $result-type)))))

; Substitution at the predicate (lam z (eq num (add zero k) z)) transports
; (refl (add zero k)) along the source proof at k: identity evidence built
; from the realization of subst@num, fed to the successor step.
!(let $package (set:native-proof zero-add)
  (let $linked (set:native-link identity $package)
   (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
    (let ((Realized $ind $c1 $r1) (Realized $refl $c2 $r2) (Realized $subst $c3 $r3)) $rows
     (let (SetNativeUseV1 $used $consumer $application $result-type)
          (set:native-use $linked
            (Lam $type (Lam (DeclConst num)
              (App (App (DeclConst sucStep) (idx 0))
                (App (App (App (App (App $r3
                       (Lam (DeclConst num)
                         (App (App (DeclConst eq@num)
                                   (App (App (DeclConst add) (DeclConst zero)) (idx 1)))
                              (idx 0))))
                       (App (App (DeclConst add) (DeclConst zero)) (idx 0)))
                     (idx 0))
                   (App (idx 1) (idx 0)))
                 (Refl (App (App (DeclConst add) (DeclConst zero)) (idx 0))))))))
      (TransportedConsumer $result-type))))))

; Closed instances compute: the consumer at two, and the transport at two.
!(let $package (set:native-proof zero-add)
  (let $linked (set:native-link identity $package)
   (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
    (let $two (App (DeclConst suc) (App (DeclConst suc) (DeclConst zero)))
     (let (SetNativeNormalFormV1 $used $consumer $application $normal $original $result)
          (set:native-normalize $linked
            (Lam $type (App (App (DeclConst sucStep) $two) (App (idx 0) $two))))
      (ClosedConsumer $normal $result))))))
!(let $package (set:native-proof zero-add)
  (let $linked (set:native-link identity $package)
   (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
    (let ((Realized $ind $c1 $r1) (Realized $refl $c2 $r2) (Realized $subst $c3 $r3)) $rows
     (let $two (App (DeclConst suc) (App (DeclConst suc) (DeclConst zero)))
      (let (SetNativeNormalFormV1 $used $consumer $application $normal $original $result)
           (set:native-normalize $linked
             (Lam $type
               (App (App (App (App (App $r3
                      (Lam (DeclConst num)
                        (App (App (DeclConst eq@num)
                                  (App (App (DeclConst add) (DeclConst zero)) $two))
                             (idx 0))))
                      (App (App (DeclConst add) (DeclConst zero)) $two))
                    $two)
                  (App (idx 0) $two))
                (Refl (App (App (DeclConst add) (DeclConst zero)) $two)))))
       (ClosedTransport $normal $result)))))))

; Controls.
; Without the interpretation the same consumer is not accepted.
!(let $package (set:native-proof zero-add)
  (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
   (let $result (try (set:native-use $package
           (Lam $type (Lam (DeclConst num)
             (App (App (DeclConst sucStep) (idx 0)) (App (idx 1) (idx 0)))))))
    (Unlinked (car-atom $result)))))
; Reflexivity cannot take the place of the source proof at an open index.
!(let $package (set:native-proof zero-add)
  (let $linked (set:native-link identity $package)
   (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
    (let $result (try (set:native-use $linked
            (Lam $type (Lam (DeclConst num)
              (App (App (DeclConst sucStep) (idx 0)) (Refl (idx 0)))))))
     (ReflexivityAtOpenIndex (car-atom $result))))))
; The evidence at zero is not evidence at one.
!(let $package (set:native-proof zero-add)
  (let $linked (set:native-link identity $package)
   (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
    (let $result (try (set:native-use $linked
            (Lam $type (App (App (DeclConst sucStep) (App (DeclConst suc) (DeclConst zero)))
                            (App (idx 0) (DeclConst zero))))))
     (WrongIndex (car-atom $result))))))
; Only the named interpretation links.
!(let $package (set:native-proof zero-add)
  (let $result (try (set:native-link classical $package))
   (UnknownInterpretation $result)))
; A linked package whose term was replaced is rejected.
!(let $package (set:native-proof zero-add)
  (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows)
       (set:native-link identity $package)
   (let $forged (SetNativeLinkedV1 $interpretation $checked (Refl $term) $type $context $rules $rows)
    (let $result (try (set:native-use $forged (Lam $type (idx 0))))
     (ForgedLink (car-atom $result))))))

; An assumption without a realization stays declared and is named: function
; extensionality is read through the identity type but is not realized.
!(set:axiom &self funext-num
   (all (-> num num) (lam f (all (-> num num) (lam g
     (imp (all num (lam x (eq num (f x) (g x)))) (eq (-> num num) f g)))))))
!(let $package (set:native-proof funext-num)
  (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows)
       (set:native-link identity $package)
   (let ((Assumed $name $constant)) $rows
    (let (SetNativeUseV1 $used $consumer $application $result-type)
         (set:native-use (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows)
                         (Lam $type (idx 0)))
     (PartiallyLinked $name (== $term (DeclConst $constant)) $result-type)))))

; An equation is realized by reflexivity exactly when its sides are
; definitionally equal: a defining equation of add and eta are; zero on the
; left of add is not, and neither is extensionality of propositions.
!(set:axiom &self add-zero-right (all num (lam n (eq num (add n zero) n))))
!(set:axiom &self add-zero-left (all num (lam n (eq num (add zero n) n))))
!(set:axiom &self eta-num (all (-> num num) (lam f (eq (-> num num) f (lam x (f x))))))
!(set:axiom &self propext
   (all prop (lam p (all prop (lam q (imp (imp p q) (imp (imp q p) (eq prop p q))))))))
!(map-atom (add-zero-right add-zero-left eta-num propext funext-num) $law
   (let $package (set:native-proof $law)
    (let (SetNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows)
         (set:native-link identity $package)
     (let ($row) $rows (Reading $law (car-atom $row))))))

; A theory that declares the identity reading links automatically: the plain
; package serves the open-index consumer directly.
!(let $result (try (set:interpret &self classical)) (UnknownReading (car-atom $result)))
!(set:interpret &self identity)
!(let $package (set:native-proof zero-add)
  (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
   (let (SetNativeUseV1 $used $consumer $application $result-type)
        (set:native-use $package
          (Lam $type (Lam (DeclConst num)
            (App (App (DeclConst sucStep) (idx 0)) (App (idx 1) (idx 0))))))
    (let (SetNativeLinkedV1 $interpretation $checked $linked $linked-type $linked-context $linked-rules $rows) $used
     (AutomaticallyLinked $interpretation (== $checked $package) $result-type)))))
!(let $package (set:native-proof zero-add)
  (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
   (let $two (App (DeclConst suc) (App (DeclConst suc) (DeclConst zero)))
    (let (SetNativeNormalFormV1 $used $consumer $application $normal $original $result)
         (set:native-normalize $package
           (Lam $type (App (App (DeclConst sucStep) $two) (App (idx 0) $two))))
     (AutomaticallyComputed $normal)))))
; Withdrawing the declaration withdraws the reading.
!(let $removed (remove-atom &self (set:interpretation identity))
  (let $package (set:native-proof zero-add)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
    (let $result (try (set:native-use $package
            (Lam $type (Lam (DeclConst num)
              (App (App (DeclConst sucStep) (idx 0)) (App (idx 1) (idx 0)))))))
     (ReadingWithdrawn (car-atom $result))))))
