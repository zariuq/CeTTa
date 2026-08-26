; Generic lowering from raw proof labels and byte codes to the neutral trace
; instruction language.  A guest supplies only label-to-instruction facts and
; byte-class facts.  Arithmetic is defined relationally over unary naturals so
; the runtime does not assign meanings to byte ranges, radices, or indices.

(gslt-presentation-v1 CertificateGSLTTraceCompilerV1
  (signature
    (operator ProofTraceCodeConsV1 2)
    (operator ProofTraceCompileCompressedAcceptedV1 5)
    (operator ProofTraceCompileCompressedIncompleteAcceptedV1 5)
    (operator ProofTraceCompileCompressedV1 4)
    (operator ProofTraceCompileDictionaryV1 3)
    (operator ProofTraceCompileInstructionListV1 2)
    (operator ProofTraceDecodeFirstRefusalV1 4)
    (operator ProofTraceHeaderAdmittedV1 2)
    (operator ProofTraceHeaderAllDifferentFromV1 2)
    (operator ProofTraceHeaderFirstOverlapV1 3)
    (operator ProofTraceHeaderListsDisjointV1 2)
    (operator ProofTraceHeaderMemberV1 2)
    (operator ProofTraceCompileNormalAcceptedV1 3)
    (operator ProofTraceCompileNormalIncompleteAcceptedV1 3)
    (operator ProofTraceCompileNormalStatusV1 4)
    (operator ProofTraceCompileNormalV1 2)
    (operator ProofTraceDecodeV1 4)
    (operator ProofTraceDecodeStatusV1 7)
    (operator ProofTraceDecodeCodeClassV1 8)
    (operator ProofTraceDictionaryResolveV1 3)
    (operator ProofTraceDictionaryResolveIndexV1 3)
    (operator ProofTraceInstructionConsV1 2)
    (operator ProofTraceInstructionListAppendV1 3)
    (operator ProofTraceLabelConsV1 2)
    (operator ProofTraceNatAddV1 3)
    (operator ProofTraceNatAliasCanonicalV1 2)
    (operator ProofTraceNatAliasExpansionCanonicalV1 2)
    (operator ProofTraceNatCanonicalV1 2)
    (operator ProofTraceNatMulAddV1 4)
    (operator ProofTraceNatMulV1 3)
    (operator ProofTraceNatSuccV1 1)
    (operator ProofTraceSavePhaseTransitionV1 2)
    (operator ProofTraceSavePlacementTransitionV1 2)
    (operator ProofTraceTerminalCodeV1 2)
    (operator ProofTraceContinuationCodeV1 2)
    (operator source-proof-trace-code-class-v1 2)
    (operator source-proof-trace-continuation-code-v1 3)
    (operator source-proof-trace-accept-incomplete-compressed-byte-v1 1)
    (operator source-proof-trace-accept-incomplete-normal-byte-v1 1)
    (operator source-proof-trace-input-incomplete-normal-label-v1 1)
    (operator source-proof-trace-input-label-different-v1 2)
    (operator source-proof-trace-label-instruction-v1 2)
    (operator source-proof-trace-nat-alias-v1 2)
    (operator source-proof-trace-save-code-v1 1)
    (operator source-proof-trace-save-placement-v1 1)
    (operator source-proof-trace-terminal-code-v1 3)
  )
  (equations)
  (rewrites
    (rule proof-trace-nat-canonical-zero-v1
      (head
        (ProofTraceNatCanonicalV1
          ProofTraceNatZeroV1 ProofTraceNatZeroV1))
      (body))
    (rule proof-trace-nat-canonical-succ-v1
      (head
        (ProofTraceNatCanonicalV1
          (ProofTraceNatSuccV1 ?source)
          (ProofTraceNatSuccV1 ?canonical)))
      (body
        (ProofTraceNatCanonicalV1 ?source ?canonical)))
    (rule proof-trace-nat-alias-canonical-v1
      (head
        (ProofTraceNatAliasCanonicalV1 ?alias ?canonical))
      (body
        (source-proof-trace-nat-alias-v1 ?alias ?source)
        (ProofTraceNatAliasExpansionCanonicalV1 ?source ?canonical)))
    (rule proof-trace-nat-alias-expansion-zero-v1
      (head
        (ProofTraceNatAliasExpansionCanonicalV1
          ProofTraceNatZeroV1 ProofTraceNatZeroV1))
      (body))
    (rule proof-trace-nat-alias-expansion-succ-v1
      (head
        (ProofTraceNatAliasExpansionCanonicalV1
          (ProofTraceNatSuccV1 ?predecessor-alias)
          (ProofTraceNatSuccV1 ?canonical-predecessor)))
      (body
        (ProofTraceNatAliasCanonicalV1
          ?predecessor-alias ?canonical-predecessor)))

    (rule proof-trace-nat-add-zero-v1
      (head
        (ProofTraceNatAddV1
          ProofTraceNatZeroV1 ?right ?right))
      (body))
    (rule proof-trace-nat-add-succ-v1
      (head
        (ProofTraceNatAddV1
          (ProofTraceNatSuccV1 ?left) ?right
          (ProofTraceNatSuccV1 ?sum)))
      (body
        (ProofTraceNatAddV1 ?left ?right ?sum)))

    (rule proof-trace-nat-mul-zero-v1
      (head
        (ProofTraceNatMulV1
          ProofTraceNatZeroV1 ?right ProofTraceNatZeroV1))
      (body))
    (rule proof-trace-nat-mul-succ-v1
      (head
        (ProofTraceNatMulV1
          (ProofTraceNatSuccV1 ?left) ?right ?product))
      (body
        (ProofTraceNatMulV1 ?left ?right ?partial)
        (ProofTraceNatAddV1 ?right ?partial ?product)))
    (rule proof-trace-nat-mul-add-v1
      (head
        (ProofTraceNatMulAddV1 ?radix ?value ?digit ?result))
      (body
        (ProofTraceNatMulV1 ?radix ?value ?scaled)
        (ProofTraceNatAddV1 ?scaled ?digit ?result)))

    (rule proof-trace-instruction-list-append-nil-v1
      (head
        (ProofTraceInstructionListAppendV1
          ProofTraceInstructionNilV1 ?right ?right))
      (body))
    (rule proof-trace-instruction-list-append-cons-v1
      (head
        (ProofTraceInstructionListAppendV1
          (ProofTraceInstructionConsV1 ?head ?tail) ?right
          (ProofTraceInstructionConsV1 ?head ?output)))
      (body
        (ProofTraceInstructionListAppendV1 ?tail ?right ?output)))

    (rule proof-trace-compile-instruction-list-nil-v1
      (head
        (ProofTraceCompileInstructionListV1
          ProofTraceLabelNilV1 ProofTraceInstructionNilV1))
      (body))
    (rule proof-trace-compile-instruction-list-cons-v1
      (head
        (ProofTraceCompileInstructionListV1
          (ProofTraceLabelConsV1 ?label ?labels)
          (ProofTraceInstructionConsV1 ?instruction ?instructions)))
      (body
        (source-proof-trace-label-instruction-v1 ?label ?instruction)
        (ProofTraceCompileInstructionListV1 ?labels ?instructions)))
    (rule proof-trace-header-all-different-nil-v1
      (head
        (ProofTraceHeaderAllDifferentFromV1
          ?label ProofTraceLabelNilV1))
      (body))
    (rule proof-trace-header-all-different-cons-v1
      (head
        (ProofTraceHeaderAllDifferentFromV1
          ?label (ProofTraceLabelConsV1 ?head ?tail)))
      (body
        (source-proof-trace-input-label-different-v1 ?label ?head)
        (ProofTraceHeaderAllDifferentFromV1 ?label ?tail)))
    (rule proof-trace-header-member-head-v1
      (head
        (ProofTraceHeaderMemberV1
          ?label (ProofTraceLabelConsV1 ?label ?tail)))
      (body))
    (rule proof-trace-header-member-tail-v1
      (head
        (ProofTraceHeaderMemberV1
          ?label (ProofTraceLabelConsV1 ?head ?tail)))
      (body
        (source-proof-trace-input-label-different-v1 ?label ?head)
        (ProofTraceHeaderMemberV1 ?label ?tail)))
    (rule proof-trace-header-first-overlap-head-v1
      (head
        (ProofTraceHeaderFirstOverlapV1
          ?initial (ProofTraceLabelConsV1 ?label ?explicit) ?label))
      (body
        (ProofTraceHeaderMemberV1 ?label ?initial)))
    (rule proof-trace-header-first-overlap-tail-v1
      (head
        (ProofTraceHeaderFirstOverlapV1
          ?initial (ProofTraceLabelConsV1 ?label ?explicit) ?overlap))
      (body
        (ProofTraceHeaderAllDifferentFromV1 ?label ?initial)
        (ProofTraceHeaderFirstOverlapV1 ?initial ?explicit ?overlap)))
    (rule proof-trace-header-lists-disjoint-nil-v1
      (head
        (ProofTraceHeaderListsDisjointV1
          ?initial ProofTraceLabelNilV1))
      (body))
    (rule proof-trace-header-lists-disjoint-cons-v1
      (head
        (ProofTraceHeaderListsDisjointV1
          ?initial (ProofTraceLabelConsV1 ?label ?explicit)))
      (body
        (ProofTraceHeaderAllDifferentFromV1 ?label ?initial)
        (ProofTraceHeaderListsDisjointV1 ?initial ?explicit)))
    (rule proof-trace-header-admitted-v1
      (head
        (ProofTraceHeaderAdmittedV1 ?initial ?explicit))
      (body
        (ProofTraceHeaderListsDisjointV1 ?initial ?explicit)))
    (rule proof-trace-compile-dictionary-v1
      (head
        (ProofTraceCompileDictionaryV1
          ?initial-labels ?explicit-labels ?dictionary))
      (body
        (ProofTraceHeaderAdmittedV1 ?initial-labels ?explicit-labels)
        (ProofTraceCompileInstructionListV1
          ?initial-labels ?initial-instructions)
        (ProofTraceCompileInstructionListV1
          ?explicit-labels ?explicit-instructions)
        (ProofTraceInstructionListAppendV1
          ?initial-instructions ?explicit-instructions ?dictionary)))

    (rule proof-trace-dictionary-resolve-v1
      (head
        (ProofTraceDictionaryResolveV1
          (ProofTraceNatSuccV1 ?index) ?dictionary ?instruction))
      (body
        (ProofTraceDictionaryResolveIndexV1
          ?index ?dictionary ?instruction)))
    (rule proof-trace-dictionary-resolve-index-zero-v1
      (head
        (ProofTraceDictionaryResolveIndexV1
          ProofTraceNatZeroV1
          (ProofTraceInstructionConsV1 ?instruction ?rest)
          ?instruction))
      (body))
    (rule proof-trace-dictionary-resolve-index-succ-v1
      (head
        (ProofTraceDictionaryResolveIndexV1
          (ProofTraceNatSuccV1 ?index)
          (ProofTraceInstructionConsV1 ?head ?rest)
          ?instruction))
      (body
        (ProofTraceDictionaryResolveV1
          (ProofTraceNatSuccV1 ?index) ?rest ?instruction)))
    (rule proof-trace-dictionary-resolve-index-saved-v1
      (head
        (ProofTraceDictionaryResolveIndexV1
          ?index
          ProofTraceInstructionNilV1
          (ProofTraceRestoreV1 ?index)))
      (body))

    (rule proof-trace-decode-v1
      (head
        (ProofTraceDecodeV1
          ?dictionary ?codes ?accumulator ?trace))
      (body
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ?accumulator ProofTracePhaseBetweenV1
          ProofTraceNoTargetV1
          ProofTraceStatusCompleteV1 ?trace)))
    (rule proof-trace-decode-status-nil-between-v1
      (head
        (ProofTraceDecodeStatusV1
          ?dictionary ProofTraceCodeNilV1 ProofTraceNatZeroV1
          ProofTracePhaseBetweenV1
          ?target ProofTraceStatusCompleteV1 ProofTraceNilV1))
      (body))
    (rule proof-trace-decode-status-nil-completed-v1
      (head
        (ProofTraceDecodeStatusV1
          ?dictionary ProofTraceCodeNilV1 ProofTraceNatZeroV1
          ProofTracePhaseJustCompletedV1
          ?target ProofTraceStatusCompleteV1 ProofTraceNilV1))
      (body))
    (rule proof-trace-decode-status-cons-v1
      (head
        (ProofTraceDecodeStatusV1
          ?dictionary
          (ProofTraceCodeConsV1 ?code ?codes)
          ?accumulator ?phase ?target ?status ?trace))
      (body
        (source-proof-trace-code-class-v1 ?code ?class)
        (ProofTraceDecodeCodeClassV1
          ?class ?dictionary ?codes ?accumulator ?phase ?target ?status
          ?trace)))
    (rule proof-trace-decode-class-terminal-v1
      (head
        (ProofTraceDecodeCodeClassV1
          (ProofTraceTerminalCodeV1 ?digit ?radix)
          ?dictionary ?codes ?accumulator ?phase ?target ?status
          (ProofTraceConsV1 ?instruction ?trace)))
      (body
        (ProofTraceNatAliasCanonicalV1 ?digit ?canonical-digit)
        (ProofTraceNatAliasCanonicalV1 ?radix ?canonical-radix)
        (ProofTraceNatMulAddV1
          ?canonical-radix ?accumulator ?canonical-digit ?number)
        (ProofTraceDictionaryResolveV1
          ?number ?dictionary ?instruction)
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ProofTraceNatZeroV1
          ProofTracePhaseJustCompletedV1
          ?target ?status ?trace)))
    (rule proof-trace-decode-class-continuation-v1
      (head
        (ProofTraceDecodeCodeClassV1
          (ProofTraceContinuationCodeV1 ?digit ?radix)
          ?dictionary ?codes ?accumulator ?phase ?target ?status ?trace))
      (body
        (ProofTraceNatAliasCanonicalV1 ?digit ?canonical-digit)
        (ProofTraceNatAliasCanonicalV1 ?radix ?canonical-radix)
        (ProofTraceNatMulAddV1
          ?canonical-radix ?accumulator ?canonical-digit
          ?next-accumulator)
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ?next-accumulator
          ProofTracePhaseOpenIndexV1
          ?target ?status ?trace)))
    (rule proof-trace-save-phase-immediate-v1
      (head
        (ProofTraceSavePhaseTransitionV1
          ProofTracePhaseJustCompletedV1 ?next-phase))
      (body
        (source-proof-trace-save-placement-v1 ?placement)
        (ProofTraceSavePlacementTransitionV1 ?placement ?next-phase)))
    (rule proof-trace-save-placement-immediate-v1
      (head
        (ProofTraceSavePlacementTransitionV1
          state-proof-save-immediately-after-use-v1
          ProofTracePhaseBetweenV1))
      (body))
    (rule proof-trace-save-placement-repeatable-v1
      (head
        (ProofTraceSavePlacementTransitionV1
          state-proof-save-repeatable-v1
          ProofTracePhaseJustCompletedV1))
      (body))
    (rule proof-trace-decode-class-save-v1
      (head
        (ProofTraceDecodeCodeClassV1
          ProofTraceSaveCodeV1 ?dictionary ?codes
          ProofTraceNatZeroV1 ?phase ?target ?status
          (ProofTraceConsV1 ProofTraceSaveV1 ?trace)))
      (body
        (ProofTraceSavePhaseTransitionV1 ?phase ?next-phase)
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ProofTraceNatZeroV1
          ?next-phase
          ?target ?status ?trace)))
    (rule proof-trace-decode-class-incomplete-between-v1
      (head
        (ProofTraceDecodeCodeClassV1
          ProofTraceIncompleteCodeV1 ?dictionary ?codes
          ProofTraceNatZeroV1 ProofTracePhaseBetweenV1
          ?target ProofTraceStatusIncompleteV1
          (ProofTraceConsV1 (ProofTraceIncompleteStepV1 ?target) ?trace)))
      (body
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ProofTraceNatZeroV1
          ProofTracePhaseBetweenV1
          ?target ?tail-status ?trace)))
    (rule proof-trace-decode-class-incomplete-completed-v1
      (head
        (ProofTraceDecodeCodeClassV1
          ProofTraceIncompleteCodeV1 ?dictionary ?codes
          ProofTraceNatZeroV1 ProofTracePhaseJustCompletedV1
          ?target ProofTraceStatusIncompleteV1
          (ProofTraceConsV1 (ProofTraceIncompleteStepV1 ?target) ?trace)))
      (body
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ProofTraceNatZeroV1
          ProofTracePhaseBetweenV1
          ?target ?tail-status ?trace)))

    ; The positive decoder above is intentionally partial: malformed save
    ; placement has no successful trace.  This companion relation traverses
    ; the same code classes and reports the first such refusal without
    ; assigning any guest byte a meaning of its own.
    (rule proof-trace-decode-first-refusal-terminal-v1
      (head
        (ProofTraceDecodeFirstRefusalV1
          (ProofTraceCodeConsV1 ?code ?codes)
          ?accumulator ?phase ?reason))
      (body
        (source-proof-trace-code-class-v1
          ?code (ProofTraceTerminalCodeV1 ?digit ?radix))
        (ProofTraceNatAliasCanonicalV1 ?digit ?canonical-digit)
        (ProofTraceNatAliasCanonicalV1 ?radix ?canonical-radix)
        (ProofTraceNatMulAddV1
          ?canonical-radix ?accumulator ?canonical-digit ?number)
        (ProofTraceDecodeFirstRefusalV1
          ?codes ProofTraceNatZeroV1
          ProofTracePhaseJustCompletedV1 ?reason)))
    (rule proof-trace-decode-first-refusal-continuation-v1
      (head
        (ProofTraceDecodeFirstRefusalV1
          (ProofTraceCodeConsV1 ?code ?codes)
          ?accumulator ?phase ?reason))
      (body
        (source-proof-trace-code-class-v1
          ?code (ProofTraceContinuationCodeV1 ?digit ?radix))
        (ProofTraceNatAliasCanonicalV1 ?digit ?canonical-digit)
        (ProofTraceNatAliasCanonicalV1 ?radix ?canonical-radix)
        (ProofTraceNatMulAddV1
          ?canonical-radix ?accumulator ?canonical-digit
          ?next-accumulator)
        (ProofTraceDecodeFirstRefusalV1
          ?codes ?next-accumulator ProofTracePhaseOpenIndexV1 ?reason)))
    (rule proof-trace-decode-first-refusal-save-valid-v1
      (head
        (ProofTraceDecodeFirstRefusalV1
          (ProofTraceCodeConsV1 ?code ?codes)
          ProofTraceNatZeroV1 ProofTracePhaseJustCompletedV1 ?reason))
      (body
        (source-proof-trace-code-class-v1 ?code ProofTraceSaveCodeV1)
        (ProofTraceSavePhaseTransitionV1
          ProofTracePhaseJustCompletedV1 ?next-phase)
        (ProofTraceDecodeFirstRefusalV1
          ?codes ProofTraceNatZeroV1 ?next-phase ?reason)))
    (rule proof-trace-decode-first-refusal-save-nonzero-v1
      (head
        (ProofTraceDecodeFirstRefusalV1
          (ProofTraceCodeConsV1 ?code ?codes)
          (ProofTraceNatSuccV1 ?accumulator) ?phase
          (ProofTraceRefusedSaveDuringIndexV1
            (ProofTraceNatSuccV1 ?accumulator))))
      (body
        (source-proof-trace-code-class-v1 ?code ProofTraceSaveCodeV1)))
    (rule proof-trace-decode-first-refusal-save-open-zero-v1
      (head
        (ProofTraceDecodeFirstRefusalV1
          (ProofTraceCodeConsV1 ?code ?codes)
          ProofTraceNatZeroV1 ProofTracePhaseOpenIndexV1
          (ProofTraceRefusedSaveDuringIndexV1 ProofTraceNatZeroV1)))
      (body
        (source-proof-trace-code-class-v1 ?code ProofTraceSaveCodeV1)))
    (rule proof-trace-decode-first-refusal-save-between-v1
      (head
        (ProofTraceDecodeFirstRefusalV1
          (ProofTraceCodeConsV1 ?code ?codes)
          ProofTraceNatZeroV1 ProofTracePhaseBetweenV1
          (ProofTraceRefusedSavePlacementV1 ProofTracePhaseBetweenV1)))
      (body
        (source-proof-trace-code-class-v1 ?code ProofTraceSaveCodeV1)))

    (rule proof-trace-compile-normal-v1
      (head
        (ProofTraceCompileNormalV1
          ?labels ?trace))
      (body
        (ProofTraceCompileNormalStatusV1
          ?labels ProofTraceNoTargetV1
          ProofTraceStatusCompleteV1 ?trace)))
    (rule proof-trace-compile-normal-status-nil-v1
      (head
        (ProofTraceCompileNormalStatusV1
          ProofTraceLabelNilV1 ?target
          ProofTraceStatusCompleteV1 ProofTraceNilV1))
      (body))
    (rule proof-trace-compile-normal-status-cons-v1
      (head
        (ProofTraceCompileNormalStatusV1
          (ProofTraceLabelConsV1 ?label ?labels)
          ?target ?status
          (ProofTraceConsV1 ?instruction ?trace)))
      (body
        (source-proof-trace-label-instruction-v1 ?label ?instruction)
        (ProofTraceCompileNormalStatusV1
          ?labels ?target ?status ?trace)))
    (rule proof-trace-compile-normal-status-incomplete-v1
      (head
        (ProofTraceCompileNormalStatusV1
          (ProofTraceLabelConsV1 ?label ?labels)
          ?target ProofTraceStatusIncompleteV1
          (ProofTraceConsV1 (ProofTraceIncompleteStepV1 ?target) ?trace)))
      (body
        (source-proof-trace-input-incomplete-normal-label-v1 ?label)
        (ProofTraceCompileNormalStatusV1
          ?labels ?target ?tail-status ?trace)))
    (rule proof-trace-compile-compressed-v1
      (head
        (ProofTraceCompileCompressedV1
          ?initial-labels ?explicit-labels ?codes ?trace))
      (body
        (ProofTraceCompileDictionaryV1
          ?initial-labels ?explicit-labels ?dictionary)
        (ProofTraceDecodeV1
          ?dictionary ?codes ProofTraceNatZeroV1 ?trace)))

    (rule proof-trace-compile-normal-accepted-v1
      (head
        (ProofTraceCompileNormalAcceptedV1
          ?context ?labels ?target))
      (body
        (ProofTraceCompileNormalV1 ?labels ?trace)
        (ProofTraceAcceptedV1 ?context ?trace ?target)))
    (rule proof-trace-compile-normal-incomplete-accepted-v1
      (head
        (ProofTraceCompileNormalIncompleteAcceptedV1
          ?context ?labels ?target))
      (body
        (ProofTraceCompileNormalStatusV1
          ?labels ?target ProofTraceStatusIncompleteV1 ?trace)
        (ProofTraceAcceptedV1 ?context ?trace ?target)))
    (rule proof-trace-compile-compressed-accepted-v1
      (head
        (ProofTraceCompileCompressedAcceptedV1
          ?context ?initial-labels ?explicit-labels ?codes ?target))
      (body
        (ProofTraceCompileCompressedV1
          ?initial-labels ?explicit-labels ?codes ?trace)
        (ProofTraceAcceptedV1 ?context ?trace ?target)))
    (rule proof-trace-compile-compressed-incomplete-accepted-v1
      (head
        (ProofTraceCompileCompressedIncompleteAcceptedV1
          ?context ?initial-labels ?explicit-labels ?codes ?target))
      (body
        (ProofTraceCompileDictionaryV1
          ?initial-labels ?explicit-labels ?dictionary)
        (ProofTraceDecodeStatusV1
          ?dictionary ?codes ProofTraceNatZeroV1
          ProofTracePhaseBetweenV1 ?target
          ProofTraceStatusIncompleteV1 ?trace)
        (ProofTraceAcceptedV1 ?context ?trace ?target)))
  ))
