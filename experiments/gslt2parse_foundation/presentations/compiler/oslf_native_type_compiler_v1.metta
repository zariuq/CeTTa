; Derive the finite presentation of the native type theory of any reflected
; finite-Horn GSLT package.  This compiler is independent of guest vocabulary.
;
; A Horn clause is a backward goal-reduction schema: its head goal reduces to
; its ordered premise goals under the clause substitution.  OSLF supplies the
; existential forward diamond, universal predecessor box, and their Galois
; connection over that generated step relation.  Spatial head signatures are
; projected from the admitted source signatures, including declarations that
; do not occur in any rewrite.

(gslt-presentation-v1 OSLFNativeTypeCompilerV1
  (signature
    (operator compile-oslf-native-type-v1 1)
    (operator oslf-carrier-v1 1)
    (operator oslf-external-relation-v1 3)
    (operator oslf-galois-v1 2)
    (operator oslf-head-signature-v1 2)
    (operator oslf-modal-v1 2)
    (operator oslf-native-former-v1 2)
    (operator oslf-step-schema-v1 4)
    (operator oslf-transition-semantics-v1 1)
  )
  (equations)
  (rewrites
    (rule oslf-native-carrier
      (head
        (compile-oslf-native-type-v1
          (oslf-carrier-v1 finite-horn-goal-state-v1)))
      (body))

    (rule oslf-native-former-top
      (head
        (compile-oslf-native-type-v1
          (oslf-native-former-v1 ntt-top-v1 q-zero)))
      (body))
    (rule oslf-native-former-bottom
      (head
        (compile-oslf-native-type-v1
          (oslf-native-former-v1 ntt-bottom-v1 q-zero)))
      (body))
    (rule oslf-native-former-conjunction
      (head
        (compile-oslf-native-type-v1
          (oslf-native-former-v1
            ntt-conjunction-v1 (q-succ (q-succ q-zero)))))
      (body))
    (rule oslf-native-former-disjunction
      (head
        (compile-oslf-native-type-v1
          (oslf-native-former-v1
            ntt-disjunction-v1 (q-succ (q-succ q-zero)))))
      (body))
    (rule oslf-native-former-headed
      (head
        (compile-oslf-native-type-v1
          (oslf-native-former-v1
            ntt-headed-v1 (q-succ (q-succ q-zero)))))
      (body))
    (rule oslf-native-former-diamond
      (head
        (compile-oslf-native-type-v1
          (oslf-native-former-v1 ntt-diamond-v1 (q-succ q-zero))))
      (body))
    (rule oslf-native-former-box
      (head
        (compile-oslf-native-type-v1
          (oslf-native-former-v1 ntt-box-v1 (q-succ q-zero))))
      (body))

    (rule oslf-native-transition-semantics
      (head
        (compile-oslf-native-type-v1
          (oslf-transition-semantics-v1
            finite-horn-backward-goal-reduction-v1)))
      (body))
    (rule oslf-native-diamond
      (head
        (compile-oslf-native-type-v1
          (oslf-modal-v1 ntt-diamond-v1 forward-existential-v1)))
      (body))
    (rule oslf-native-box
      (head
        (compile-oslf-native-type-v1
          (oslf-modal-v1 ntt-box-v1 predecessor-universal-v1)))
      (body))
    (rule oslf-native-galois
      (head
        (compile-oslf-native-type-v1
          (oslf-galois-v1 ntt-diamond-v1 ntt-box-v1)))
      (body))

    (rule oslf-native-step-schema
      (head
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1 ?owner ?rule ?head ?body)))
      (body
        (source-rule
          ?owner
          (q-rule (q-sym ?rule) ?head ?body))))

    (rule oslf-native-external-relation
      (head
        (compile-oslf-native-type-v1
          (oslf-external-relation-v1
            ?owner (q-sym ?relation) (q-int ?arity))))
      (body
        (source-rule
          ?owner
          (q-rule
            ?declaration
            (q-app
              (q-sym oslf-external-relation-decl-v1)
              (q-cons
                (q-sym ?relation)
                (q-cons (q-int ?arity) q-nil)))
            q-nil))))

    (rule oslf-native-head-signature-from-declaration
      (head
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?constructor ?arity)))
      (body
        (source-operator
          ?owner (q-sym ?constructor) ?arity)))
  ))
