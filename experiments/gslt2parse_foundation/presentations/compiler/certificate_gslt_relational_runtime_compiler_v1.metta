; Compile a relational proof projection to a store/request provider program.
; The emitted instructions name every source table and every constructor used
; at the native boundary.  A runtime interprets these records without knowing
; the guest language or assigning meaning to table names.

(gslt-presentation-v1 CertificateGSLTRelationalRuntimeCompilerV1
  (signature
    (operator proof-relational-runtime-artifact-v1 1)
    (operator proof-relational-runtime-identity-v1 2)
    (operator proof-relational-runtime-state-table-v1 5)
    (operator proof-relational-runtime-ordinal-column-v1 5)
    (operator proof-relational-runtime-control-literal-v1 4)
    (operator proof-relational-runtime-distinct-pairs-v1 4)
    (operator proof-relational-runtime-binary-table-enumeration-v1 8)
    (operator proof-relational-runtime-value-list-v1 4)
    (operator proof-relational-runtime-request-v1 3)
    (operator proof-relational-runtime-prefix-count-v1 7)
    (operator proof-relational-runtime-input-byte-v1 4)
    (operator proof-relational-runtime-normal-input-v1 5)
    (operator proof-relational-runtime-compressed-input-v1 11)
    (operator proof-relational-runtime-outcome-query-v1 5)
  )
  (equations)
  (rewrites
    (rule compile-proof-relational-runtime-identity-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-identity-v1 ?owner ?base)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))

    (rule compile-proof-relational-runtime-row2-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-state-table-v1
            ?owner source-proof-relational-row2-v1 ?table 2 ?key-arity)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner ?role ?table 2 ?key-arity)))
    (rule compile-proof-relational-runtime-row3-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-state-table-v1
            ?owner source-proof-relational-row3-v1 ?table 3 ?key-arity)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner ?role ?table 3 ?key-arity)))
    (rule compile-proof-relational-runtime-row4-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-state-table-v1
            ?owner source-proof-relational-row4-v1 ?table 4 ?key-arity)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner ?role ?table 4 ?key-arity)))

    ; Ordered state rows carry their source ordinal as canonical decimal
    ; bytes.  The proof trace uses the native Peano carrier, so the generated
    ; provider plan names that representation change explicitly.
    (rule compile-proof-relational-runtime-ordered-ordinal-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-ordinal-column-v1
            ?owner ?table 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-context-v1 ?table 3 2)))
    (rule compile-proof-relational-runtime-frame-ordinal-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-ordinal-column-v1
            ?owner ?table 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-frame-v1 ?table 3 2)))
    (rule compile-proof-relational-runtime-apartness-ordinal-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-ordinal-column-v1
            ?owner ?table 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-assertion-apartness-v1 ?table 4 2)))

    ; State values are opaque guest bytes unless the authored projection
    ; declares a control literal used by its own rules.  These records make
    ; that separation explicit so the runtime never guesses from spellings.
    (rule compile-proof-relational-runtime-token-literal-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-control-literal-v1
            ?owner ?table 1 ?literal)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-symbol-class-v1 ?table 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-token-literal-v1 ?literal)))
    (rule compile-proof-relational-runtime-token-variable-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-control-literal-v1
            ?owner ?table 1 ?variable)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-symbol-class-v1 ?table 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-token-variable-v1 ?variable)))
    (rule compile-proof-relational-runtime-hypothesis-binding-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-control-literal-v1
            ?owner ?table 1 ?binding)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-label-class-v1 ?table 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-hypothesis-binding-v1 ?binding)))
    (rule compile-proof-relational-runtime-hypothesis-essential-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-control-literal-v1
            ?owner ?table 1 ?essential)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-label-class-v1 ?table 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-hypothesis-essential-v1 ?essential)))
    (rule compile-proof-relational-runtime-label-assertion-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-control-literal-v1
            ?owner ?table 1 ?assertion)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-label-class-v1 ?table 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-label-assertion-v1 ?assertion)))

    ; An authored classifier may request the finite irreflexive relation over
    ; one class of opaque store values.  The generated record supplies the
    ; relation, table, columns, and discriminator; the runtime only enumerates
    ; unequal values selected by that data.
    (rule compile-proof-relational-runtime-symbol-distinct-pairs-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-distinct-pairs-v1
            ?owner source-proof-relational-value-different-v1
            ?table 0)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-symbol-class-v1 ?table 2 1)))
    (rule compile-proof-relational-runtime-label-distinct-pairs-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-distinct-pairs-v1
            ?owner source-proof-relational-value-different-v1
            ?table 0)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-label-class-v1 ?table 2 1)))

    ; Closed-world decisions stay in the authored relation algebra.  The
    ; runtime contributes only an indexed finite enumeration whose table,
    ; columns, and ordinal constructors are selected by this generated record.
    (rule compile-proof-relational-runtime-active-apartness-enumeration-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-binary-table-enumeration-v1
            ?owner source-proof-relational-active-apartness-row-v1
            source-proof-relational-active-apartness-count-v1
            ?table 0 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-active-apartness-v1 ?table 2 2)))

    (rule compile-proof-relational-runtime-value-list-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-value-list-v1
            ?owner source-proof-relational-value-list-v1
            ProofRelationalValueListConsV1
            ProofRelationalValueListNilV1)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))
    (rule compile-proof-relational-runtime-request-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-request-v1
            ?owner source-proof-relational-request-v1
            ProofRelationalRequestV1)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))

    (rule compile-proof-relational-runtime-context-count-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-prefix-count-v1
            ?owner source-proof-trace-input-context-count-v1
            ProofRelationalRequestV1 ?table
            ProofTraceNatZeroV1 ProofTraceNatSuccV1
            proof-runtime-request-label-v1)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-context-v1 ?table 3 2)))
    (rule compile-proof-relational-runtime-frame-count-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-prefix-count-v1
            ?owner source-proof-trace-input-frame-count-v1
            ProofRelationalLabelV1 ?table
            ProofTraceNatZeroV1 ProofTraceNatSuccV1
            proof-runtime-proof-labels-v1)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-frame-v1 ?table 3 2)))
    (rule compile-proof-relational-runtime-apartness-count-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-prefix-count-v1
            ?owner source-proof-trace-input-apartness-count-v1
            ProofRelationalLabelV1 ?table
            ProofTraceNatZeroV1 ProofTraceNatSuccV1
            proof-runtime-proof-labels-v1)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-assertion-apartness-v1 ?table 4 2)))

    (rule compile-proof-relational-runtime-normal-input-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-normal-input-v1
            ?owner ProofRelationalRequestV1 ProofRelationalLabelV1
            ProofTraceLabelConsV1 ProofTraceLabelNilV1)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))
    (rule compile-proof-relational-runtime-compressed-input-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-compressed-input-v1
            ?owner
            ProofRelationalRequestV1 ProofRelationalLabelV1
            ProofTraceLabelConsV1 ProofTraceLabelNilV1
            ProofTraceCodeConsV1 ProofTraceCodeNilV1
            ?ordered 0 1 2)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-frame-v1 ?ordered 3 2)))

    ; Execution tries generated outcome queries in priority order.  Result
    ; classes are part of the generic proof API; the guest supplies which
    ; query head establishes each class.
    (rule compile-proof-relational-runtime-normal-verified-query-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-outcome-query-v1
            ?owner proof-runtime-normal-input-v1 0
            proof-runtime-result-verified-v1
            ProofTraceInputVerifyNormalV1)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))
    (rule compile-proof-relational-runtime-compressed-verified-query-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-outcome-query-v1
            ?owner proof-runtime-compressed-input-v1 0
            proof-runtime-result-verified-v1
            ProofTraceInputVerifyCompressedV1)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))
    (rule compile-proof-relational-runtime-normal-incomplete-query-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-outcome-query-v1
            ?owner proof-runtime-normal-input-v1 1
            proof-runtime-result-incomplete-v1
            ProofTraceInputAcceptIncompleteNormalV1)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (source-proof-trace-accept-incomplete-normal-byte-v1 ?byte)))
    (rule compile-proof-relational-runtime-compressed-incomplete-query-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-outcome-query-v1
            ?owner proof-runtime-compressed-input-v1 1
            proof-runtime-result-incomplete-v1
            ProofTraceInputAcceptIncompleteCompressedV1)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (source-proof-trace-accept-incomplete-compressed-byte-v1 ?byte)))

    ; The generated provider maps authored input bytes to external calculus
    ; relations.  Native code performs only this declared representation
    ; bridge and never assigns a guest meaning to a byte.
    (rule compile-proof-relational-runtime-normal-incomplete-byte-v1
      (head
        (proof-relational-runtime-artifact-v1
          (proof-relational-runtime-input-byte-v1
            ?owner proof-runtime-normal-input-v1
            source-proof-trace-input-incomplete-normal-label-v1 ?byte)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (source-proof-trace-accept-incomplete-normal-byte-v1 ?byte)))
  ))
