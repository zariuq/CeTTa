; Authored HOL Light REFL in the closed Boolean lambda fragment.
; The interpretation uses predicate equality over shared STT. A dependent
; consumer observes the checked proof itself, without identifying HOL
; equality with native dependent identity.
(scope:profile lexical-fresh per-call reference)
!(import! &self ../../lib/prime/hol_light/data.metta)
!(import! &self ../../lib/prime/hol_light/syntax.metta)
!(import! &self ../../lib/prime/hol_light/kernel.metta)
!(import! &self ../../lib/prime/hol_light/definitions.metta)
!(import! &self ../../lib/prime/hol_light/derived.metta)
!(import! &self ../../lib/prime/hol_light/profile.metta)
!(import! &self ../../lib/prime/hol_light/native.metta)
(set:profile hol)

(= (native-example:identity)
  (function (return
    (Abs (Var "p" (Tyapp "bool" HOL:Nil))
         (Var "p" (Tyapp "bool" HOL:Nil))))))

(= (native-example:function-type)
  (hol-light:fun (hol-light:bool) (hol-light:bool)))

(= (native-example:application)
  (let $function-type (native-example:function-type)
    (function (return
      (Abs (Var "f" $function-type)
        (Abs (Var "x" (Tyapp "bool" HOL:Nil))
          (Comb (Var "f" $function-type)
            (Comb (Var "f" $function-type) (Var "x" (Tyapp "bool" HOL:Nil))))))))))

; A lambda at an application head retains its source domain annotation.
(= (native-example:lambda-application)
  (let $function-type (native-example:function-type)
    (let $identity (native-example:identity)
      (function (return
        (Comb (Abs (Var "f" $function-type) (Var "f" $function-type)) $identity))))))

(= (native-example:nested)
  (let $function-type (native-example:function-type)
    (function (return
      (Abs (Var "f" $function-type)
        (Abs (Var "x" (Tyapp "bool" HOL:Nil))
          (Comb (Var "f" $function-type) (Var "x" (Tyapp "bool" HOL:Nil)))))))))

(= (native-example:dependent $packet)
  (case $packet
    (((HOL:NativeReflexivityV1 PredicateEquality $space $state $source $received
        $carrier $code $proposition
        (SetNativeProofV1 $name $phi $proof $term $type $context $rules $assumptions $digest))
       (case (hol-light:native-normalize $packet (Lam $type (Refl (idx 0))))
         (((HOL:NativeNormalFormV1 $receipt
             (SetNativeNormalFormV1 $checked $consumer $application
               (Refl $normal-proof) $before (Id $actual-type $left $right)))
            (if (== $assumptions ())
              (if (== $left $normal-proof) (== $right $normal-proof) False) False))
          ($refused False))))
     ($unsupported False))))

(= (native-example:wrong-index $packet)
  (case $packet
    (((HOL:NativeReflexivityV1 PredicateEquality $space $state $source $received
        $carrier $code $proposition $package)
       (hol-light:native-validate
         (HOL:NativeReflexivityV1 PredicateEquality $space $state $source $received
           $carrier (lam _ (idx 1)) $proposition $package)))
     ($unsupported None))))

(= (native-example:forged-proof $packet)
  (case $packet
    (((HOL:NativeReflexivityV1 PredicateEquality $space $state $source $received
        $carrier $code $proposition
        (SetNativeProofV1 $name $phi $proof $term $type $context $rules $assumptions $digest))
       (case (hol-light:native-use
               (HOL:NativeReflexivityV1 PredicateEquality $space $state $source $received
                 $carrier $code $proposition
                 (SetNativeProofV1 $name $phi $proof (idx 9) $type
                   $context $rules $assumptions $digest))
               (Lam $type (Refl (idx 0))))
         (((HOL:NativeUseV1 $receipt False) True)
          ($other False))))
     ($unsupported False))))

!(NativeIdentity
   (native-example:dependent
     (hol-light:native-reflexivity &self native-identity
       (hol-light:initial HOL:Nil) (native-example:identity))))
!(NativeApplication
   (native-example:dependent
     (hol-light:native-reflexivity &self native-application
       (hol-light:initial HOL:Nil) (native-example:application))))
!(NativeNested
   (native-example:dependent
     (hol-light:native-reflexivity &self native-nested
       (hol-light:initial HOL:Nil) (native-example:nested))))
!(NativeWrongIndex
   (== (native-example:wrong-index
         (hol-light:native-reflexivity &self native-index-control
           (hol-light:initial HOL:Nil) (native-example:identity))) None))
!(NativeForgedProof
   (native-example:forged-proof
     (hol-light:native-reflexivity &self native-forgery-control
       (hol-light:initial HOL:Nil) (native-example:identity))))
!(NativeOpenTerm
   (case (hol-light:native-reflexivity &self native-open-control
           (hol-light:initial HOL:Nil) (Var "p" (Tyapp "bool" HOL:Nil)))
     (((HOL:NativeUnsupported $state $source (Some $theorem)) True)
      ($other False))))
!(NativeMalformedSource
   (case (hol-light:native-reflexivity &self native-malformed-control
           (hol-light:initial HOL:Nil)
           (Comb (native-example:identity) (native-example:identity)))
     (((HOL:NativeRefused $state $source) True) ($other False))))
!(NativeUnboundIndex
   (== (hol-light:native-term HOL:Nil
         (HOL:Bound 0 (Tyapp "bool" HOL:Nil))) None))
!(NativeLambdaApplication
   (native-example:dependent
     (hol-light:native-reflexivity &self native-lambda-application
       (hol-light:initial HOL:Nil) (native-example:lambda-application))))
