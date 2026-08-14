; Derive proof-call storage and escape plans from admitted state, proof,
; relational-ABI, and semantic-NTT artifacts.  The compiler is independent
; of guest vocabulary: table roles and proof-sequence roles provide the only
; links between a proof machine, its persistent inputs, and its semantic
; judgment.

(gslt-presentation-v1 ProofStoragePlanCompilerV1
  (signature
    (operator compile-oslf-native-type-v1 1)
    (operator compile-proof-storage-plan-v1 1)
    (operator oslf-head-signature-v1 2)
    (operator proof-artifact-v1 1)
    (operator proof-action-id-v1 2)
    (operator proof-calculus-constructor-v1 3)
    (operator proof-call-id-v1 2)
    (operator proof-call-region-plan-v1 8)
    (operator proof-workspace-plan-v1 6)
    (operator proof-repetition-cache-plan-v1 8)
    (operator proof-extension-v1 2)
    (operator proof-judgment-v1 3)
    (operator proof-machine-binding-v1 4)
    (operator proof-machine-id-v1 1)
    (operator proof-machine-table-read-v1 4)
    (operator proof-finite-support-plan-v1 11)
    (operator proof-frame-index-plan-v1 6)
    (operator proof-indexed-effect-machine-plan-v1 9)
    (operator proof-indexed-program-plan-v1 24)
    (operator proof-prepared-action-case-v1 3)
    (operator proof-prepared-action-machine-v1 3)
    (operator proof-prepared-action-valid-v1 1)
    (operator proof-indexed-value-plan-v1 7)
    (operator proof-literal-hole-plan-v1 7)
    (operator proof-two-phase-frame-plan-v1 10)
    (operator proof-sequence-evidence-artifact-v1 1)
    (operator proof-sequence-evidence-assertion-judgment-role-v1 3)
    (operator proof-sequence-evidence-constructor-role-v1 4)
    (operator proof-sequence-evidence-judgment-role-v1 3)
    (operator proof-sequence-evidence-rule-role-v1 3)
    (operator proof-sequence-id-v1 2)
    (operator proof-sequence-layout-v1 7)
    (operator proof-sequence-relational-artifact-v1 1)
    (operator proof-sequence-relational-identity-v1 2)
    (operator proof-sequence-relational-table-v1 5)
    (operator proof-storage-denoted-v1 1)
    (operator proof-storage-key-v1 2)
    (operator proof-storage-region-v1 2)
    (operator proof-storage-required-v1 1)
    (operator proof-succ-v1 1)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-sym 1)
    (operator q-succ 1)
    (operator q-var 1)
    (operator oslf-step-schema-v1 4)
    (operator state-action-v1 3)
    (operator state-byte-literal-v1 1)
    (operator state-check-proof-compressed-v1 5)
    (operator state-check-proof-normal-v1 4)
    (operator state-literal-v1 1)
    (operator state-proof-decoder-v1 13)
    (operator state-proof-action-v1 3)
    (operator state-proof-kinds-v1 6)
    (operator state-proof-machine-v1 2)
    (operator state-proof-tables-v1 9)
    (operator state-stack-proof-machine-v1 3)
    (operator state-table-id-v1 1)
    (operator state-table-storage-v1 5)
  )
  (equations)
  (rewrites
    (rule proof-storage-region-persistent-v1
      (head
        (proof-storage-region-v1
          state-persistent-v1 state-run-region-v1))
      (body))
    (rule proof-storage-region-scoped-v1
      (head
        (proof-storage-region-v1
          state-scoped-v1 state-scope-region-v1))
      (body))
    (rule proof-storage-region-transactional-v1
      (head
        (proof-storage-region-v1
          state-transactional-v1 state-transaction-region-v1))
      (body))

    (rule proof-storage-table-v1
      (head
        (compile-proof-storage-plan-v1
          (state-table-storage-v1
            ?table ?arity ?key-arity ?lifetime ?region)))
      (body
        (state-table-v1 ?table ?arity ?key-arity ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))

    (rule proof-storage-machine-read-label-kind-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine label-kind-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?table ?formula ?binder ?mandatory ?hypothesis
              ?disjoint ?active-hypothesis ?active-disjoint ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 2 1 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-formula-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine formula-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?table ?binder ?mandatory ?hypothesis
              ?disjoint ?active-hypothesis ?active-disjoint ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 2 1 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-binder-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine floating-variable-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?formula ?table ?mandatory ?hypothesis
              ?disjoint ?active-hypothesis ?active-disjoint ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 2 1 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-mandatory-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine mandatory-variable-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?formula ?binder ?table ?hypothesis
              ?disjoint ?active-hypothesis ?active-disjoint ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 2 2 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-assertion-hypothesis-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine ordered-hypothesis-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?formula ?binder ?mandatory ?table
              ?disjoint ?active-hypothesis ?active-disjoint ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 3 2 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-assertion-disjoint-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine assertion-disjoint-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?formula ?binder ?mandatory ?hypothesis
              ?table ?active-hypothesis ?active-disjoint ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 3 3 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-active-hypothesis-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine active-hypothesis-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?formula ?binder ?mandatory ?hypothesis
              ?disjoint ?table ?active-disjoint ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 4 1 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-active-disjoint-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine active-disjoint-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?formula ?binder ?mandatory ?hypothesis
              ?disjoint ?active-hypothesis ?table ?symbol)
            ?kinds ?decoder))
        (state-table-v1 ?table 2 2 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))
    (rule proof-storage-machine-read-symbol-kind-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine symbol-kind-v1 ?table ?lifetime)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label ?formula ?binder ?mandatory ?hypothesis
              ?disjoint ?active-hypothesis ?active-disjoint ?table)
            ?kinds ?decoder))
        (state-table-v1 ?table 2 1 ?lifetime)
        (proof-storage-region-v1 ?lifetime ?region)))

    (rule proof-storage-sequence-layout-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-sequence-layout-v1
            ?owner ?base ?provable ?cons ?nil
            flat-symbol-id-vector-v1 proof-call-region-v1)))
      (body
        (proof-artifact-v1 (proof-extension-v1 ?owner ?base))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-assertion-judgment-role-v1
            ?owner provable-v1 ?provable))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-constructor-role-v1
            ?owner ?base sequence-cons-v1 ?cons))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-constructor-role-v1
            ?owner ?base sequence-nil-v1 ?nil))
        (proof-artifact-v1
          (proof-judgment-v1
            ?owner ?provable (proof-succ-v1 proof-zero-v1)))
        (proof-artifact-v1
          (proof-calculus-constructor-v1
            ?owner ?cons
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
        (proof-artifact-v1
          (proof-calculus-constructor-v1
            ?owner ?nil proof-zero-v1))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?provable (q-succ q-zero)))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1
            ?cons (q-succ (q-succ q-zero))))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?nil q-zero))))

    ; An immutable flat sequence can compile adjacent literal cells into runs
    ; while retaining dense substitution slots as holes.  The compiler derives
    ; the permission from the sequence layout and persistent frame inputs; the
    ; runtime supplies only the vocabulary-independent run interpreter.
    (rule proof-storage-literal-hole-program-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-literal-hole-plan-v1
            ?machine ?owner ?cons ?nil
            literal-hole-run-program-v1 state-run-region-v1
            proof-call-region-v1)))
      (body
        (compile-proof-storage-plan-v1
          (proof-machine-binding-v1
            ?machine ?owner ?base ?provable))
        (compile-proof-storage-plan-v1
          (proof-sequence-layout-v1
            ?owner ?base ?provable ?cons ?nil
            flat-symbol-id-vector-v1 proof-call-region-v1))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine formula-v1 ?formula state-persistent-v1))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine mandatory-variable-v1 ?mandatory state-persistent-v1))))

    ; A proof call may use the generic two-phase frame machine when its local
    ; presentation exposes an exact stack call, a split binder/matching proof
    ; kind inventory, immutable formula/frame inputs, and an admitted flat
    ; literal/hole template.  Apartness is intentionally not part of this
    ; record: a separately generated finite-support plan may refine the base
    ; machine, while a guest without apartness still admits the same carrier.
    (rule proof-storage-two-phase-frame-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-two-phase-frame-plan-v1
            ?operation ?index ?machine
            two-phase-frame-machine-v1 ?template-carrier
            literal-head-optional-v1 epoch-stamped-dense-slots-v1
            unique-dense-binders-v1 exact-stack-suffix-v1 ?region)))
      (body
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            ?region flat-symbol-id-vector-v1 proof-verdict-only-v1))
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            ?tables
            (state-proof-kinds-v1
              ?binder-kind ?matching-kind ?rule-kind-first ?rule-kind-second
              ?variable-kind ?unknown-token)
            ?decoder))
        (compile-proof-storage-plan-v1
          (proof-literal-hole-plan-v1
            ?machine ?owner ?cons ?nil ?template-carrier
            state-run-region-v1 ?region))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine formula-v1 ?formula state-persistent-v1))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine mandatory-variable-v1 ?mandatory state-persistent-v1))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine ordered-hypothesis-v1 ?hypothesis state-persistent-v1))))

    ; A finite support carrier is admitted only for the complete recursive
    ; sequence-apartness fragment.  The evidence roles bind the authored
    ; calculus to these native-type schemas; the schemas then prevent a role
    ; label from licensing a differently shaped rule.
    (rule proof-storage-finite-support-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-finite-support-plan-v1
            ?owner ?cons ?nil ?support-apart ?token-against
            ?pair-allowed ?apart ?literal ?variable
            finite-dense-bitset-v1 finite-apartness-matrix-v1)))
      (body
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-constructor-role-v1
            ?owner ?base sequence-cons-v1 ?cons))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-constructor-role-v1
            ?owner ?base sequence-nil-v1 ?nil))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-judgment-role-v1
            ?owner support-apart-v1 ?support-apart))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-judgment-role-v1
            ?owner token-against-sequence-v1 ?token-against))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-judgment-role-v1
            ?owner pair-allowed-v1 ?pair-allowed))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-judgment-role-v1
            ?owner apart-v1 ?apart))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-judgment-role-v1
            ?owner literal-v1 ?literal))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-judgment-role-v1
            ?owner variable-v1 ?variable))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner support-apart-cons-v1 ?support-cons-rule))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner support-apart-nil-v1 ?support-nil-rule))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner token-against-cons-v1 ?token-cons-rule))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner token-against-nil-v1 ?token-nil-rule))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner pair-apart-v1 ?pair-apart-rule))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner pair-left-literal-v1 ?pair-left-literal-rule))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner pair-right-literal-v1 ?pair-right-literal-rule))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?cons (q-succ (q-succ q-zero))))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?nil q-zero))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1
            ?support-apart (q-succ (q-succ q-zero))))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1
            ?token-against (q-succ (q-succ q-zero))))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1
            ?pair-allowed (q-succ (q-succ q-zero))))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?apart (q-succ (q-succ q-zero))))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?literal (q-succ q-zero)))
        (compile-oslf-native-type-v1
          (oslf-head-signature-v1 ?variable (q-succ q-zero)))
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1
            ?owner ?support-cons-rule
            (q-app (q-sym ?support-apart)
              (q-cons
                (q-app (q-sym ?cons)
                  (q-cons (q-var q-zero)
                    (q-cons (q-var (q-succ q-zero)) q-nil)))
                (q-cons (q-var (q-succ (q-succ q-zero))) q-nil)))
            (q-cons
              (q-app (q-sym ?token-against)
                (q-cons (q-var q-zero)
                  (q-cons (q-var (q-succ (q-succ q-zero))) q-nil)))
              (q-cons
                (q-app (q-sym ?support-apart)
                  (q-cons (q-var (q-succ q-zero))
                    (q-cons (q-var (q-succ (q-succ q-zero))) q-nil)))
                q-nil))))
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1
            ?owner ?support-nil-rule
            (q-app (q-sym ?support-apart)
              (q-cons (q-app (q-sym ?nil) q-nil)
                (q-cons (q-var q-zero) q-nil)))
            q-nil))
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1
            ?owner ?token-cons-rule
            (q-app (q-sym ?token-against)
              (q-cons (q-var q-zero)
                (q-cons
                  (q-app (q-sym ?cons)
                    (q-cons (q-var (q-succ q-zero))
                      (q-cons (q-var (q-succ (q-succ q-zero))) q-nil)))
                  q-nil)))
            (q-cons
              (q-app (q-sym ?pair-allowed)
                (q-cons (q-var q-zero)
                  (q-cons (q-var (q-succ q-zero)) q-nil)))
              (q-cons
                (q-app (q-sym ?token-against)
                  (q-cons (q-var q-zero)
                    (q-cons (q-var (q-succ (q-succ q-zero))) q-nil)))
                q-nil))))
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1
            ?owner ?token-nil-rule
            (q-app (q-sym ?token-against)
              (q-cons (q-var q-zero)
                (q-cons (q-app (q-sym ?nil) q-nil) q-nil)))
            q-nil))
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1
            ?owner ?pair-apart-rule
            (q-app (q-sym ?pair-allowed)
              (q-cons (q-var q-zero)
                (q-cons (q-var (q-succ q-zero)) q-nil)))
            (q-cons
              (q-app (q-sym ?variable)
                (q-cons (q-var q-zero) q-nil))
              (q-cons
                (q-app (q-sym ?variable)
                  (q-cons (q-var (q-succ q-zero)) q-nil))
                (q-cons
                  (q-app (q-sym ?apart)
                    (q-cons (q-var q-zero)
                      (q-cons (q-var (q-succ q-zero)) q-nil)))
                  q-nil)))))
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1
            ?owner ?pair-left-literal-rule
            (q-app (q-sym ?pair-allowed)
              (q-cons (q-var q-zero)
                (q-cons (q-var (q-succ q-zero)) q-nil)))
            (q-cons
              (q-app (q-sym ?literal)
                (q-cons (q-var q-zero) q-nil))
              q-nil)))
        (compile-oslf-native-type-v1
          (oslf-step-schema-v1
            ?owner ?pair-right-literal-rule
            (q-app (q-sym ?pair-allowed)
              (q-cons (q-var q-zero)
                (q-cons (q-var (q-succ q-zero)) q-nil)))
            (q-cons
              (q-app (q-sym ?literal)
                (q-cons (q-var (q-succ q-zero)) q-nil))
              q-nil)))))

    (rule proof-storage-machine-binding-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-machine-binding-v1
            ?machine ?owner ?base ?provable)))
      (body
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label-kind ?formula ?binder ?mandatory
              ?assertion-hypothesis ?assertion-disjoint
              ?active-hypothesis ?active-disjoint ?symbol-kind)
            ?kinds ?decoder))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-identity-v1 ?owner ?base))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner label-kind-v1 ?label-kind 2 1))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner formula-v1 ?formula 2 1))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner floating-variable-v1 ?binder 2 1))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner mandatory-variable-v1 ?mandatory 2 2))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner ordered-hypothesis-v1
            ?assertion-hypothesis 3 2))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner assertion-disjoint-v1 ?assertion-disjoint 3 3))
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner symbol-kind-v1 ?symbol-kind 2 1))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine active-hypothesis-v1
            ?active-hypothesis ?active-hypothesis-lifetime))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine active-disjoint-v1
            ?active-disjoint ?active-disjoint-lifetime))
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-assertion-judgment-role-v1
            ?owner provable-v1 ?provable))
        (compile-proof-storage-plan-v1
          (proof-sequence-layout-v1
            ?owner ?base ?provable ?cons ?nil
            flat-symbol-id-vector-v1 proof-call-region-v1))))

    (rule proof-storage-call-normal-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            proof-call-region-v1 flat-symbol-id-vector-v1
            proof-verdict-only-v1)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-normal-v1
            ?machine ?label-role ?formula-role ?proof-role))
        (compile-proof-storage-plan-v1
          (proof-machine-binding-v1
            ?machine ?owner ?base ?provable))))
    (rule proof-storage-call-compressed-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            proof-call-region-v1 flat-symbol-id-vector-v1
            proof-verdict-only-v1)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role))
        (compile-proof-storage-plan-v1
          (proof-machine-binding-v1
            ?machine ?owner ?base ?provable))))

    ; Reset-and-reuse is a property of the compiled call machine, not of the
    ; guest's proof-sequence representation.  A workspace is emitted only
    ; after the frame compiler has established the exact generic machine shape
    ; and a call-local execution region.  The runtime may promote repeated
    ; declarations only after the physical store supplies immutable-prefix
    ; snapshots for the declaration inputs.  A failed snapshot stays on the
    ; transient source path, so no invalidation policy is invented and no
    ; singleton declaration is retained.
    (rule proof-storage-workspace-normal-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-workspace-plan-v1
            ?operation ?index ?machine
            stack-proof-call-workspace-v1 ?region ?observation)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-normal-v1
            ?machine ?label-role ?formula-role ?proof-role))
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            ?region ?layout ?observation))
        (compile-proof-storage-plan-v1
          (proof-two-phase-frame-plan-v1
            ?operation ?index ?machine two-phase-frame-machine-v1
            literal-hole-run-program-v1 literal-head-optional-v1
            epoch-stamped-dense-slots-v1 unique-dense-binders-v1
            exact-stack-suffix-v1 ?region))))
    (rule proof-storage-workspace-compressed-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-workspace-plan-v1
            ?operation ?index ?machine
            indexed-stack-proof-call-workspace-v1
            ?region ?observation)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role))
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            ?region ?layout ?observation))
        (compile-proof-storage-plan-v1
          (proof-two-phase-frame-plan-v1
            ?operation ?index ?machine two-phase-frame-machine-v1
            literal-hole-run-program-v1 literal-head-optional-v1
            epoch-stamped-dense-slots-v1 unique-dense-binders-v1
            exact-stack-suffix-v1 ?region))))

    ; The generated plan, rather than workspace existence alone, selects the
    ; generic repetition policy.  Promotion remains conditional on a live
    ; immutable-prefix snapshot from the physical store.
    (rule proof-storage-repetition-cache-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-repetition-cache-plan-v1
            ?operation ?index ?machine u32-identity-key-v1
            owned-plan-value-v1 second-occurrence-admission-v1
            immutable-prefix-snapshot-v1 ?region)))
      (body
        (compile-proof-storage-plan-v1
          (proof-workspace-plan-v1
            ?operation ?index ?machine ?workspace ?region ?observation))
        (compile-proof-storage-plan-v1
          (proof-two-phase-frame-plan-v1
            ?operation ?index ?machine two-phase-frame-machine-v1
            ?template-carrier literal-head-optional-v1
            epoch-stamped-dense-slots-v1 unique-dense-binders-v1
            exact-stack-suffix-v1 ?region))))

    ; A prepared indexed-value table is admitted only when the complete
    ; ordered header and the indexed instruction stream are distinct inputs
    ; of one bounded proof call, the machine supplies an explicit numeric
    ; decoder, and its finite value classifier is persistent.  The runtime
    ; may therefore retain each value with its validated classifier result
    ; rather than re-querying the immutable table on every use.
    (rule proof-storage-indexed-value-table-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-indexed-value-plan-v1
            ?operation ?index ?machine ?header-role ?code-role
            prepared-classified-value-table-v1 ?region)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role))
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            ?tables
            (state-proof-kinds-v1
              ?binder-kind ?matching-kind ?rule-kind-first ?rule-kind-second
              ?variable-kind ?unknown-token)
            (state-proof-decoder-v1
              ?terminal-low ?terminal-high
              ?continuation-low ?continuation-high
              ?save ?unknown
              ?terminal-radix ?terminal-bias
              ?continuation-radix ?continuation-bias
              ?unknown-policy ?save-placement ?header-policy)))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine label-kind-v1 ?classifier state-persistent-v1))
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            ?region flat-symbol-id-vector-v1
            proof-verdict-only-v1))))

    ; A compressed call may fuse its admitted decoder and split indexed-value
    ; carrier into one streaming effect machine.  The generated record keeps
    ; the effect algebra explicit: prepared and saved values remain distinct,
    ; save acts on the current top, and the authored unknown policy is carried
    ; unchanged.  The runtime implements only this closed generic interface.
    (rule proof-storage-indexed-effect-machine-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-indexed-effect-machine-plan-v1
            ?operation ?index ?machine
            indexed-effect-machine-v1
            use-prepared-value-v1 use-saved-value-v1 save-top-value-v1
            ?unknown-policy ?region)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role))
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            ?tables ?kinds
            (state-proof-decoder-v1
              ?terminal-low ?terminal-high
              ?continuation-low ?continuation-high
              ?save ?unknown
              ?terminal-radix ?terminal-bias
              ?continuation-radix ?continuation-bias
              ?unknown-policy ?save-placement ?header-policy)))
        (compile-proof-storage-plan-v1
          (proof-indexed-value-plan-v1
            ?operation ?index ?machine ?header-role ?code-role
            prepared-classified-value-table-v1 ?region))
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            ?region flat-symbol-id-vector-v1 proof-verdict-only-v1))))

    ; The public compressed-proof program is a generated product of the
    ; authored call, decoder, indexed-value, and effect plans.  The runtime
    ; receives one closed program record rather than reconstructing decoder
    ; policy or effect selection from guest knowledge.
    (rule proof-storage-indexed-program-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-indexed-program-plan-v1
            ?operation ?index ?machine ?header-role ?code-role
            ?terminal-low ?terminal-high
            ?continuation-low ?continuation-high
            ?save ?unknown
            ?terminal-radix ?terminal-bias
            ?continuation-radix ?continuation-bias
            ?unknown-policy ?save-placement ?header-policy
            prepared-classified-value-table-v1
            indexed-effect-machine-v1
            use-prepared-value-v1 use-saved-value-v1 save-top-value-v1
            ?region)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role))
        (state-proof-machine-v1
          ?machine
          (state-stack-proof-machine-v1
            ?tables ?kinds
            (state-proof-decoder-v1
              ?terminal-low ?terminal-high
              ?continuation-low ?continuation-high
              ?save ?unknown
              ?terminal-radix ?terminal-bias
              ?continuation-radix ?continuation-bias
              ?unknown-policy ?save-placement ?header-policy)))
        (compile-proof-storage-plan-v1
          (proof-indexed-value-plan-v1
            ?operation ?index ?machine ?header-role ?code-role
            prepared-classified-value-table-v1 ?region))
        (compile-proof-storage-plan-v1
          (proof-indexed-effect-machine-plan-v1
            ?operation ?index ?machine
            indexed-effect-machine-v1
            use-prepared-value-v1 use-saved-value-v1 save-top-value-v1
            ?unknown-policy ?region))))

    ; Action selection is a generated finite relation from the guest's
    ; authored source-kind values to closed generic stack actions.  The
    ; runtime sees the resulting list as data and does not carry a positional
    ; inventory of binder/matching/rule roles.
    (rule proof-storage-prepared-action-push-valid-v1
      (head
        (proof-prepared-action-valid-v1 stack-push-declared-v1))
      (body))
    (rule proof-storage-prepared-action-apply-valid-v1
      (head
        (proof-prepared-action-valid-v1 stack-apply-frame-v1))
      (body))
    (rule proof-storage-prepared-action-v1
      (head
        (proof-prepared-action-machine-v1
          ?machine ?source-kind ?action))
      (body
        (state-proof-action-v1 ?machine ?source-kind ?action)
        (proof-prepared-action-valid-v1 ?action)))
    (rule proof-storage-prepared-action-case-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-prepared-action-case-v1
            ?machine ?source-role ?action)))
      (body
        (proof-prepared-action-machine-v1
          ?machine ?source-role ?action)))

    ; Frame preparation repeatedly resolves finite symbol identities while
    ; constructing one proof-call-local dictionary.  The two selected table
    ; roles and the bounded call region license a dynamically validated
    ; monotone unique-key index.  Duplicate keys remain refusals: the carrier
    ; does not silently choose one occurrence.
    (rule proof-storage-frame-index-v1
      (head
        (compile-proof-storage-plan-v1
          (proof-frame-index-plan-v1
            ?operation ?index ?machine
            u32-open-addressed-index-v1 duplicate-reject-v1
            ?region)))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine mandatory-variable-v1
            ?mandatory-table ?mandatory-lifetime))
        (compile-proof-storage-plan-v1
          (proof-machine-table-read-v1
            ?machine ordered-hypothesis-v1
            ?hypothesis-table ?hypothesis-lifetime))
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            ?region flat-symbol-id-vector-v1
            proof-verdict-only-v1))))

    (rule proof-storage-required-table-v1
      (head
        (proof-storage-required-v1
          (proof-storage-key-v1
            state-table-v1 (state-table-id-v1 ?table))))
      (body
        (state-table-v1 ?table ?arity ?key-arity ?lifetime)))
    (rule proof-storage-denoted-table-v1
      (head
        (proof-storage-denoted-v1
          (proof-storage-key-v1
            state-table-v1 (state-table-id-v1 ?table))))
      (body
        (compile-proof-storage-plan-v1
          (state-table-storage-v1
            ?table ?arity ?key-arity ?lifetime ?region))))

    (rule proof-storage-required-machine-v1
      (head
        (proof-storage-required-v1
          (proof-storage-key-v1
            proof-machine-v1 (proof-machine-id-v1 ?machine))))
      (body
        (state-proof-machine-v1 ?machine ?configuration)))
    (rule proof-storage-denoted-machine-v1
      (head
        (proof-storage-denoted-v1
          (proof-storage-key-v1
            proof-machine-v1 (proof-machine-id-v1 ?machine))))
      (body
        (compile-proof-storage-plan-v1
          (proof-machine-binding-v1
            ?machine ?owner ?base ?provable))))

    (rule proof-storage-required-action-v1
      (head
        (proof-storage-required-v1
          (proof-storage-key-v1
            proof-action-v1
            (proof-action-id-v1 ?machine ?source-kind))))
      (body
        (state-proof-action-v1 ?machine ?source-kind ?action)))
    (rule proof-storage-denoted-action-v1
      (head
        (proof-storage-denoted-v1
          (proof-storage-key-v1
            proof-action-v1
            (proof-action-id-v1 ?machine ?source-kind))))
      (body
        (compile-proof-storage-plan-v1
          (proof-prepared-action-case-v1
            ?machine ?source-kind ?action))))

    (rule proof-storage-required-sequence-v1
      (head
        (proof-storage-required-v1
          (proof-storage-key-v1
            proof-sequence-v1
            (proof-sequence-id-v1 ?owner ?provable))))
      (body
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-assertion-judgment-role-v1
            ?owner provable-v1 ?provable))))
    (rule proof-storage-denoted-sequence-v1
      (head
        (proof-storage-denoted-v1
          (proof-storage-key-v1
            proof-sequence-v1
            (proof-sequence-id-v1 ?owner ?provable))))
      (body
        (compile-proof-storage-plan-v1
          (proof-sequence-layout-v1
            ?owner ?base ?provable ?cons ?nil ?layout ?region))))

    (rule proof-storage-required-call-normal-v1
      (head
        (proof-storage-required-v1
          (proof-storage-key-v1
            proof-call-v1 (proof-call-id-v1 ?operation ?index))))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-normal-v1
            ?machine ?label-role ?formula-role ?proof-role))))
    (rule proof-storage-required-call-compressed-v1
      (head
        (proof-storage-required-v1
          (proof-storage-key-v1
            proof-call-v1 (proof-call-id-v1 ?operation ?index))))
      (body
        (state-action-v1
          ?operation ?index
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role))))
    (rule proof-storage-denoted-call-v1
      (head
        (proof-storage-denoted-v1
          (proof-storage-key-v1
            proof-call-v1 (proof-call-id-v1 ?operation ?index))))
      (body
        (compile-proof-storage-plan-v1
          (proof-call-region-plan-v1
            ?operation ?index ?machine ?owner ?provable
            ?region ?layout ?observation))))
  ))
