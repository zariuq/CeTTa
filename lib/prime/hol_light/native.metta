; Prime adapter from authored HOL Light reflexivity to the existing native
; proof service. Boolean lambda terms also require admission by the scoped
; elaborator, with bool read as the shared STT proposition carrier. Equality
; is read as predicate equality; it is not identified with dependent identity.
; Import the translated HOL Light library before this module.

(= (hol-light:native-type $type)
  (case $type
    (((Tyapp "bool" HOL:Nil) (function (return (Some prop))))
     ((Tyapp "fun" (HOL:Cons $domain (HOL:Cons $codomain HOL:Nil)))
       (case ((hol-light:native-type $domain) (hol-light:native-type $codomain))
         ((((Some $a) (Some $b)) (function (return (Some (-> $a $b)))))
          ($unsupported None))))
     ($unsupported None))))

; The source binder context is retained and checked at every occurrence.
; Returned code is syntax data, including the ordinary lambda constructor.
(= (hol-light:native-term $context $term)
  (case $term
    (((HOL:Bound $index $type)
       (if (hol-light:natural $index)
         (if (== (hol-light:at $index $context) (Some $type))
           (case (hol-light:native-type $type)
             (((Some $carrier)
                (function (return (Some (HOL:NativeTerm $type $carrier (idx $index))))))
              ($unsupported None))) None) None))
     ((HOL:Lam $domain $body)
       (case ((hol-light:native-type $domain)
              (hol-light:native-term (HOL:Cons $domain $context) $body))
         ((((Some $a) (Some (HOL:NativeTerm $result $b $code)))
            (let $source-type (hol-light:fun $domain $result)
              (function (return
                (Some (HOL:NativeTerm $source-type (-> $a $b) (lam (_ : $a) $code)))))))
          ($unsupported None))))
     ((HOL:App $function $argument)
       (case ((hol-light:native-term $context $function)
              (hol-light:native-term $context $argument))
         ((((Some (HOL:NativeTerm
                    (Tyapp "fun" (HOL:Cons $domain (HOL:Cons $codomain HOL:Nil)))
                    (-> $a $b) $fn))
            (Some (HOL:NativeTerm $input $input-carrier $value)))
            (if (== $domain $input)
              (if (== $a $input-carrier)
                (function (return (Some (HOL:NativeTerm $codomain $b ($fn $value)))))
                None) None))
          ($unsupported None))))
     ($unsupported None))))

(= (hol-light:native-reflexive-proposition $carrier $code)
  (function (return
    (all (-> $carrier prop)
      (lam _ (imp ((idx 0) $code) ((idx 0) $code)))))))

(= (hol-light:native-reflexive-proof)
  (function (return (pf:all-intro (pf:imp-intro (pf:hyp 0))))))

; A theorem record supplied by the caller is never the admission path.
; The authored infer operation reads the source request in its retained state.
(= (hol-light:native-reflexivity $space $name $state $source)
  (case ($state $source)
    ((($retained-state $retained-source)
       (hol-light:native-reflexivity-values $space $name
         $retained-state $retained-source)))))

(= (hol-light:native-reflexivity-values $space $name $state $source)
  (let $received (hol-light:infer $state (REFL $source))
    (case $received
      (((Some (HOL:Thm HOL:Nil
                (HOL:App (HOL:App (HOL:C (HOL:Primitive "=") $annotation) $left) $right)
                HOL:Nil REFL))
         (if (== $left $right)
           (case (hol-light:native-term HOL:Nil $left)
             (((Some (HOL:NativeTerm $source-type $carrier $code))
                (if (== $annotation (hol-light:eq-type $source-type))
                    (let $proposition (hol-light:native-reflexive-proposition $carrier $code)
                      (let $proof (hol-light:native-reflexive-proof)
                        (let $published (set:theorem $space $name $proposition $proof)
                          (if (== $published $name)
                            (let $package (set:native-proof $space $name)
                              (function (return
                                (HOL:NativeReflexivityV1 PredicateEquality $space $state $source
                                  $received $carrier $code $proposition $package))))
                            (function (return
                              (HOL:NativePublication $space $state $source $received $published)))))))
                  (function (return (HOL:NativeUnsupported $state $source $received)))))
              ($unsupported (function (return (HOL:NativeUnsupported $state $source $received))))))
           (function (return (HOL:NativeUnsupported $state $source $received)))))
       (None (function (return (HOL:NativeRefused $state $source))))
       ($other (function (return (HOL:NativeUnsupported $state $source $received))))))))

; Validate the source/index link again before dependent use. The existing
; native service independently rechecks the package, declarations and digest.
(= (hol-light:native-validate $packet)
  (case $packet
    (((HOL:NativeReflexivityV1 PredicateEquality $space $state $source
        $received $carrier $code $proposition
        (SetNativeProofV1 $name $native-proposition $proof $term $type $context $rules $assumptions $digest))
       (let $actual (hol-light:infer $state (REFL $source))
         (if (== $received $actual)
           (case $actual
             (((Some (HOL:Thm HOL:Nil
                       (HOL:App (HOL:App (HOL:C (HOL:Primitive "=") $annotation) $left) $right)
                       HOL:Nil REFL))
                (if (== $left $right)
                  (case (hol-light:native-term HOL:Nil $left)
                    (((Some (HOL:NativeTerm $source-type $actual-carrier $actual-code))
                       (if (== $annotation (hol-light:eq-type $source-type))
                         (if (== $carrier $actual-carrier)
                           (if (== $code $actual-code)
                             (if (== $proposition (hol-light:native-reflexive-proposition $carrier $code))
                               (if (== $proof (hol-light:native-reflexive-proof))
                                 (if (== $assumptions ())
                                   (if (set:proves $space (pf:known $name) $proposition)
                                     (function (return (Some (HOL:NativeReady $space
                                       (SetNativeProofV1 $name $native-proposition $proof $term $type
                                         $context $rules $assumptions $digest)))))
                                     None) None) None) None) None) None) None))
                     ($unsupported None))) None))
              ($refused None))) None)))
     ($malformed None))))

(= (hol-light:native-use $packet $consumer)
  (case (hol-light:native-validate $packet)
    (((Some (HOL:NativeReady $space $package))
       (let $used (set:native-use $space $package $consumer)
         (function (return (HOL:NativeUseV1 $packet $used)))))
     ($refused (function (return (HOL:NativeUseRefused $packet)))))))

(= (hol-light:native-normalize $packet $consumer)
  (case (hol-light:native-validate $packet)
    (((Some (HOL:NativeReady $space $package))
       (let $used (set:native-normalize $space $package $consumer)
         (function (return (HOL:NativeNormalFormV1 $packet $used)))))
     ($refused (function (return (HOL:NativeUseRefused $packet)))))))
