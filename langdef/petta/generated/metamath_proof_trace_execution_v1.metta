; Generic PeTTa execution machinery for generated finite-Horn clauses.

!(import! &self (library lib_tabling))
!(tabled (horn_prove_v1 $goal))

(= (horn:prove-v1 $goal) (horn_prove_v1 $goal))

(= (horn_prove_v1 $goal)
   (match &self
     (HornClauseV1 $goal $body)
     (let $_proof (horn:prove-body-v1 $body) $goal)))

(= (horn:prove-body-v1 HornBodyNilV1) HornProvedV1)
(= (horn:prove-body-v1 (HornBodyConsV1 $goal $rest))
   (let $_goal (horn_prove_v1 $goal)
     (horn:prove-body-v1 $rest)))

(HornClauseV1 (FiniteHornPackageDigestV1 sha256-d7db079fd5b3d0ff732bd3c4a7d56e7c9f8908e9f121c67d7dc212f7eae0df38) HornBodyNilV1)
(HornClauseV1 (MetamathProvableV1 (ProofSequenceConsV1 $v2 $v8)) (HornBodyConsV1 (ProofAssertionDeclaredV1 (ProofAssertionV1 $v0 $v1 $v2 $v3 $v4)) (HornBodyConsV1 (ProofAssertionBuildEnvironmentV1 $v0 $v5 $v7) (HornBodyConsV1 (ProofAssertionCheckEssentialsV1 $v1 $v6 $v7) (HornBodyConsV1 (ProofAssertionCheckDisjointsV1 $v4 $v7) (HornBodyConsV1 (ProofSequenceInstantiateV1 $v3 $v7 $v8) HornBodyNilV1))))))
(HornClauseV1 (ProofAssertionBuildEnvironmentV1 (ProofAssertionListConsV1 (ProofAssertionVariableV1 $v0 $v1) $v3) (ProofAssertionListConsV1 $v2 $v4) (ProofEnvironmentConsV1 $v0 $v2 $v5)) (HornBodyConsV1 (ProofTokenVariableV1 $v0) (HornBodyConsV1 (MetamathProvableV1 (ProofSequenceConsV1 $v1 $v2)) (HornBodyConsV1 (ProofAssertionBuildEnvironmentV1 $v3 $v4 $v5) HornBodyNilV1))))
(HornClauseV1 (ProofAssertionBuildEnvironmentV1 ProofAssertionListNilV1 ProofAssertionListNilV1 ProofEnvironmentNilV1) HornBodyNilV1)
(HornClauseV1 (ProofAssertionCheckDisjointsV1 (ProofAssertionListConsV1 (ProofAssertionDisjointV1 $v0 $v1) $v2) $v3) (HornBodyConsV1 (ProofTokenDifferentV1 $v0 $v1) (HornBodyConsV1 (ProofEnvironmentLookupV1 $v3 $v0 $v4) (HornBodyConsV1 (ProofEnvironmentLookupV1 $v3 $v1 $v5) (HornBodyConsV1 (ProofSequenceSupportApartV1 $v4 $v5) (HornBodyConsV1 (ProofAssertionCheckDisjointsV1 $v2 $v3) HornBodyNilV1))))))
(HornClauseV1 (ProofAssertionCheckDisjointsV1 ProofAssertionListNilV1 $v0) HornBodyNilV1)
(HornClauseV1 (ProofAssertionCheckEssentialsV1 (ProofAssertionListConsV1 (ProofAssertionEssentialV1 $v0) $v2) (ProofAssertionListConsV1 $v1 $v3) $v4) (HornBodyConsV1 (MetamathProvableV1 $v1) (HornBodyConsV1 (ProofSequenceInstantiateV1 $v0 $v4 $v1) (HornBodyConsV1 (ProofAssertionCheckEssentialsV1 $v2 $v3 $v4) HornBodyNilV1))))
(HornClauseV1 (ProofAssertionCheckEssentialsV1 ProofAssertionListNilV1 ProofAssertionListNilV1 $v0) HornBodyNilV1)
(HornClauseV1 (MetamathProvableV1 $v0) (HornBodyConsV1 (MetamathProvableV1 $v0) HornBodyNilV1))
(HornClauseV1 (ProofEnvironmentLookupV1 (ProofEnvironmentConsV1 $v0 $v1 $v2) $v0 $v1) HornBodyNilV1)
(HornClauseV1 (ProofEnvironmentLookupV1 (ProofEnvironmentConsV1 $v0 $v1 $v2) $v3 $v4) (HornBodyConsV1 (ProofTokenDifferentV1 $v0 $v3) (HornBodyConsV1 (ProofEnvironmentLookupV1 $v2 $v3 $v4) HornBodyNilV1)))
(HornClauseV1 (ProofSequenceAppendV1 (ProofSequenceConsV1 $v0 $v1) $v2 (ProofSequenceConsV1 $v0 $v3)) (HornBodyConsV1 (ProofSequenceAppendV1 $v1 $v2 $v3) HornBodyNilV1))
(HornClauseV1 (ProofSequenceAppendV1 ProofSequenceNilV1 $v0 $v0) HornBodyNilV1)
(HornClauseV1 (ProofSequenceInstantiateV1 (ProofSequenceConsV1 $v0 $v1) $v2 (ProofSequenceConsV1 $v0 $v3)) (HornBodyConsV1 (ProofTokenLiteralV1 $v0) (HornBodyConsV1 (ProofSequenceInstantiateV1 $v1 $v2 $v3) HornBodyNilV1)))
(HornClauseV1 (ProofSequenceInstantiateV1 ProofSequenceNilV1 $v0 ProofSequenceNilV1) HornBodyNilV1)
(HornClauseV1 (ProofSequenceInstantiateV1 (ProofSequenceConsV1 $v0 $v1) $v2 $v5) (HornBodyConsV1 (ProofTokenVariableV1 $v0) (HornBodyConsV1 (ProofEnvironmentLookupV1 $v2 $v0 $v3) (HornBodyConsV1 (ProofSequenceInstantiateV1 $v1 $v2 $v4) (HornBodyConsV1 (ProofSequenceAppendV1 $v3 $v4 $v5) HornBodyNilV1)))))
(HornClauseV1 (ProofSequenceSupportApartV1 (ProofSequenceConsV1 $v0 $v1) $v2) (HornBodyConsV1 (ProofTokenAgainstSequenceV1 $v0 $v2) (HornBodyConsV1 (ProofSequenceSupportApartV1 $v1 $v2) HornBodyNilV1)))
(HornClauseV1 (ProofSequenceSupportApartV1 ProofSequenceNilV1 $v0) HornBodyNilV1)
(HornClauseV1 (ProofTokenAgainstSequenceV1 $v0 (ProofSequenceConsV1 $v1 $v2)) (HornBodyConsV1 (ProofTokenPairAllowedV1 $v0 $v1) (HornBodyConsV1 (ProofTokenAgainstSequenceV1 $v0 $v2) HornBodyNilV1)))
(HornClauseV1 (ProofTokenAgainstSequenceV1 $v0 ProofSequenceNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTokenPairAllowedV1 $v0 $v1) (HornBodyConsV1 (ProofTokenVariableV1 $v0) (HornBodyConsV1 (ProofTokenVariableV1 $v1) (HornBodyConsV1 (ProofTokenApartV1 $v0 $v1) HornBodyNilV1))))
(HornClauseV1 (ProofTokenPairAllowedV1 $v0 $v1) (HornBodyConsV1 (ProofTokenLiteralV1 $v0) HornBodyNilV1))
(HornClauseV1 (ProofTokenPairAllowedV1 $v0 $v1) (HornBodyConsV1 (ProofTokenLiteralV1 $v1) HornBodyNilV1))
(HornClauseV1 (source-proof-trace-accept-incomplete-compressed-byte-v1 63) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-accept-incomplete-normal-byte-v1 63) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-continuation-code-v1 85 ProofTraceDigitOneV1 ProofTraceRadixFiveV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-continuation-code-v1 86 ProofTraceDigitTwoV1 ProofTraceRadixFiveV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-continuation-code-v1 87 ProofTraceDigitThreeV1 ProofTraceRadixFiveV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-continuation-code-v1 88 ProofTraceDigitFourV1 ProofTraceRadixFiveV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-continuation-code-v1 89 ProofTraceDigitFiveV1 ProofTraceRadixFiveV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitEightV1 (ProofTraceNatSuccV1 ProofTraceDigitSevenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitEighteenV1 (ProofTraceNatSuccV1 ProofTraceDigitSeventeenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitElevenV1 (ProofTraceNatSuccV1 ProofTraceDigitTenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitFifteenV1 (ProofTraceNatSuccV1 ProofTraceDigitFourteenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitFiveV1 (ProofTraceNatSuccV1 ProofTraceDigitFourV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitFourV1 (ProofTraceNatSuccV1 ProofTraceDigitThreeV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitFourteenV1 (ProofTraceNatSuccV1 ProofTraceDigitThirteenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitNineV1 (ProofTraceNatSuccV1 ProofTraceDigitEightV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitNineteenV1 (ProofTraceNatSuccV1 ProofTraceDigitEighteenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitOneV1 (ProofTraceNatSuccV1 ProofTraceNatZeroV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitSevenV1 (ProofTraceNatSuccV1 ProofTraceDigitSixV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitSeventeenV1 (ProofTraceNatSuccV1 ProofTraceDigitSixteenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitSixV1 (ProofTraceNatSuccV1 ProofTraceDigitFiveV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitSixteenV1 (ProofTraceNatSuccV1 ProofTraceDigitFifteenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitTenV1 (ProofTraceNatSuccV1 ProofTraceDigitNineV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitThirteenV1 (ProofTraceNatSuccV1 ProofTraceDigitTwelveV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitThreeV1 (ProofTraceNatSuccV1 ProofTraceDigitTwoV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitTwelveV1 (ProofTraceNatSuccV1 ProofTraceDigitElevenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitTwentyV1 (ProofTraceNatSuccV1 ProofTraceDigitNineteenV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceDigitTwoV1 (ProofTraceNatSuccV1 ProofTraceDigitOneV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceRadixFiveV1 ProofTraceDigitFiveV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-nat-alias-v1 ProofTraceRadixTwentyV1 ProofTraceDigitTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-save-code-v1 90) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 65 ProofTraceDigitOneV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 66 ProofTraceDigitTwoV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 67 ProofTraceDigitThreeV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 68 ProofTraceDigitFourV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 69 ProofTraceDigitFiveV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 70 ProofTraceDigitSixV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 71 ProofTraceDigitSevenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 72 ProofTraceDigitEightV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 73 ProofTraceDigitNineV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 74 ProofTraceDigitTenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 75 ProofTraceDigitElevenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 76 ProofTraceDigitTwelveV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 77 ProofTraceDigitThirteenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 78 ProofTraceDigitFourteenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 79 ProofTraceDigitFifteenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 80 ProofTraceDigitSixteenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 81 ProofTraceDigitSeventeenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 82 ProofTraceDigitEighteenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 83 ProofTraceDigitNineteenV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-terminal-code-v1 84 ProofTraceDigitTwentyV1 ProofTraceRadixTwentyV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-save-placement-v1 state-proof-save-immediately-after-use-v1) HornBodyNilV1)
(HornClauseV1 (ProofTraceCompileCompressedAcceptedV1 $context $initial-labels $explicit-labels $codes $target) (HornBodyConsV1 (ProofTraceCompileCompressedV1 $initial-labels $explicit-labels $codes $trace) (HornBodyConsV1 (ProofTraceAcceptedV1 $context $trace $target) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCompileCompressedIncompleteAcceptedV1 $context $initial-labels $explicit-labels $codes $target) (HornBodyConsV1 (ProofTraceCompileDictionaryV1 $initial-labels $explicit-labels $dictionary) (HornBodyConsV1 (ProofTraceDecodeStatusV1 $dictionary $codes ProofTraceNatZeroV1 ProofTracePhaseBetweenV1 $target ProofTraceStatusIncompleteV1 $trace) (HornBodyConsV1 (ProofTraceAcceptedV1 $context $trace $target) HornBodyNilV1))))
(HornClauseV1 (ProofTraceCompileCompressedV1 $initial-labels $explicit-labels $codes $trace) (HornBodyConsV1 (ProofTraceCompileDictionaryV1 $initial-labels $explicit-labels $dictionary) (HornBodyConsV1 (ProofTraceDecodeV1 $dictionary $codes ProofTraceNatZeroV1 $trace) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCompileDictionaryV1 $initial-labels $explicit-labels $dictionary) (HornBodyConsV1 (ProofTraceHeaderAdmittedV1 $initial-labels $explicit-labels) (HornBodyConsV1 (ProofTraceCompileInstructionListV1 $initial-labels $initial-instructions) (HornBodyConsV1 (ProofTraceCompileInstructionListV1 $explicit-labels $explicit-instructions) (HornBodyConsV1 (ProofTraceInstructionListAppendV1 $initial-instructions $explicit-instructions $dictionary) HornBodyNilV1)))))
(HornClauseV1 (ProofTraceCompileInstructionListV1 (ProofTraceLabelConsV1 $label $labels) (ProofTraceInstructionConsV1 $instruction $instructions)) (HornBodyConsV1 (source-proof-trace-label-instruction-v1 $label $instruction) (HornBodyConsV1 (ProofTraceCompileInstructionListV1 $labels $instructions) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCompileInstructionListV1 ProofTraceLabelNilV1 ProofTraceInstructionNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceCompileNormalAcceptedV1 $context $labels $target) (HornBodyConsV1 (ProofTraceCompileNormalV1 $labels $trace) (HornBodyConsV1 (ProofTraceAcceptedV1 $context $trace $target) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCompileNormalIncompleteAcceptedV1 $context $labels $target) (HornBodyConsV1 (ProofTraceCompileNormalStatusV1 $labels $target ProofTraceStatusIncompleteV1 $trace) (HornBodyConsV1 (ProofTraceAcceptedV1 $context $trace $target) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCompileNormalStatusV1 (ProofTraceLabelConsV1 $label $labels) $target $status (ProofTraceConsV1 $instruction $trace)) (HornBodyConsV1 (source-proof-trace-label-instruction-v1 $label $instruction) (HornBodyConsV1 (ProofTraceCompileNormalStatusV1 $labels $target $status $trace) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCompileNormalStatusV1 (ProofTraceLabelConsV1 $label $labels) $target ProofTraceStatusIncompleteV1 (ProofTraceConsV1 (ProofTraceIncompleteStepV1 $target) $trace)) (HornBodyConsV1 (source-proof-trace-input-incomplete-normal-label-v1 $label) (HornBodyConsV1 (ProofTraceCompileNormalStatusV1 $labels $target $tail-status $trace) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCompileNormalStatusV1 ProofTraceLabelNilV1 $target ProofTraceStatusCompleteV1 ProofTraceNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceCompileNormalV1 $labels $trace) (HornBodyConsV1 (ProofTraceCompileNormalStatusV1 $labels ProofTraceNoTargetV1 ProofTraceStatusCompleteV1 $trace) HornBodyNilV1))
(HornClauseV1 (ProofTraceDecodeStatusV1 $dictionary (ProofTraceCodeConsV1 $code $codes) $accumulator $phase $target $status $trace) (HornBodyConsV1 (source-proof-trace-continuation-code-v1 $code $digit $radix) (HornBodyConsV1 (ProofTraceNatCanonicalV1 $digit $canonical-digit) (HornBodyConsV1 (ProofTraceNatCanonicalV1 $radix $canonical-radix) (HornBodyConsV1 (ProofTraceNatMulAddV1 $canonical-radix $accumulator $canonical-digit $next-accumulator) (HornBodyConsV1 (ProofTraceDecodeStatusV1 $dictionary $codes $next-accumulator ProofTracePhaseOpenIndexV1 $target $status $trace) HornBodyNilV1))))))
(HornClauseV1 (ProofTraceDecodeStatusV1 $dictionary (ProofTraceCodeConsV1 $code $codes) ProofTraceNatZeroV1 ProofTracePhaseBetweenV1 $target ProofTraceStatusIncompleteV1 (ProofTraceConsV1 (ProofTraceIncompleteStepV1 $target) $trace)) (HornBodyConsV1 (source-proof-trace-input-incomplete-compressed-code-v1 $code) (HornBodyConsV1 (ProofTraceDecodeStatusV1 $dictionary $codes ProofTraceNatZeroV1 ProofTracePhaseBetweenV1 $target $tail-status $trace) HornBodyNilV1)))
(HornClauseV1 (ProofTraceDecodeStatusV1 $dictionary (ProofTraceCodeConsV1 $code $codes) ProofTraceNatZeroV1 ProofTracePhaseJustCompletedV1 $target ProofTraceStatusIncompleteV1 (ProofTraceConsV1 (ProofTraceIncompleteStepV1 $target) $trace)) (HornBodyConsV1 (source-proof-trace-input-incomplete-compressed-code-v1 $code) (HornBodyConsV1 (ProofTraceDecodeStatusV1 $dictionary $codes ProofTraceNatZeroV1 ProofTracePhaseBetweenV1 $target $tail-status $trace) HornBodyNilV1)))
(HornClauseV1 (ProofTraceDecodeStatusV1 $dictionary ProofTraceCodeNilV1 ProofTraceNatZeroV1 ProofTracePhaseBetweenV1 $target ProofTraceStatusCompleteV1 ProofTraceNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceDecodeStatusV1 $dictionary ProofTraceCodeNilV1 ProofTraceNatZeroV1 ProofTracePhaseJustCompletedV1 $target ProofTraceStatusCompleteV1 ProofTraceNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceDecodeStatusV1 $dictionary (ProofTraceCodeConsV1 $code $codes) ProofTraceNatZeroV1 $phase $target $status (ProofTraceConsV1 ProofTraceSaveV1 $trace)) (HornBodyConsV1 (source-proof-trace-save-code-v1 $code) (HornBodyConsV1 (ProofTraceSavePhaseTransitionV1 $phase $next-phase) (HornBodyConsV1 (ProofTraceDecodeStatusV1 $dictionary $codes ProofTraceNatZeroV1 $next-phase $target $status $trace) HornBodyNilV1))))
(HornClauseV1 (ProofTraceDecodeStatusV1 $dictionary (ProofTraceCodeConsV1 $code $codes) $accumulator $phase $target $status (ProofTraceConsV1 $instruction $trace)) (HornBodyConsV1 (source-proof-trace-terminal-code-v1 $code $digit $radix) (HornBodyConsV1 (ProofTraceNatCanonicalV1 $digit $canonical-digit) (HornBodyConsV1 (ProofTraceNatCanonicalV1 $radix $canonical-radix) (HornBodyConsV1 (ProofTraceNatMulAddV1 $canonical-radix $accumulator $canonical-digit $number) (HornBodyConsV1 (ProofTraceDictionaryResolveV1 $number $dictionary $instruction) (HornBodyConsV1 (ProofTraceDecodeStatusV1 $dictionary $codes ProofTraceNatZeroV1 ProofTracePhaseJustCompletedV1 $target $status $trace) HornBodyNilV1)))))))
(HornClauseV1 (ProofTraceDecodeV1 $dictionary $codes $accumulator $trace) (HornBodyConsV1 (ProofTraceDecodeStatusV1 $dictionary $codes $accumulator ProofTracePhaseBetweenV1 ProofTraceNoTargetV1 ProofTraceStatusCompleteV1 $trace) HornBodyNilV1))
(HornClauseV1 (ProofTraceDictionaryResolveV1 (ProofTraceNatSuccV1 ProofTraceNatZeroV1) (ProofTraceInstructionConsV1 $instruction $rest) $instruction) HornBodyNilV1)
(HornClauseV1 (ProofTraceDictionaryResolveV1 (ProofTraceNatSuccV1 $index) ProofTraceInstructionNilV1 (ProofTraceRestoreV1 $index)) HornBodyNilV1)
(HornClauseV1 (ProofTraceDictionaryResolveV1 (ProofTraceNatSuccV1 $index) (ProofTraceInstructionConsV1 $head $rest) $instruction) (HornBodyConsV1 (ProofTraceDictionaryResolveV1 $index $rest $instruction) HornBodyNilV1))
(HornClauseV1 (ProofTraceHeaderAdmittedV1 $initial $explicit) (HornBodyConsV1 (ProofTraceHeaderListsDisjointV1 $initial $explicit) HornBodyNilV1))
(HornClauseV1 (ProofTraceHeaderAllDifferentFromV1 $label (ProofTraceLabelConsV1 $head $tail)) (HornBodyConsV1 (source-proof-trace-input-label-different-v1 $label $head) (HornBodyConsV1 (ProofTraceHeaderAllDifferentFromV1 $label $tail) HornBodyNilV1)))
(HornClauseV1 (ProofTraceHeaderAllDifferentFromV1 $label ProofTraceLabelNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceHeaderListsDisjointV1 $initial (ProofTraceLabelConsV1 $label $explicit)) (HornBodyConsV1 (ProofTraceHeaderAllDifferentFromV1 $label $initial) (HornBodyConsV1 (ProofTraceHeaderListsDisjointV1 $initial $explicit) HornBodyNilV1)))
(HornClauseV1 (ProofTraceHeaderListsDisjointV1 $initial ProofTraceLabelNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceInstructionListAppendV1 (ProofTraceInstructionConsV1 $head $tail) $right (ProofTraceInstructionConsV1 $head $output)) (HornBodyConsV1 (ProofTraceInstructionListAppendV1 $tail $right $output) HornBodyNilV1))
(HornClauseV1 (ProofTraceInstructionListAppendV1 ProofTraceInstructionNilV1 $right $right) HornBodyNilV1)
(HornClauseV1 (ProofTraceNatAddV1 (ProofTraceNatSuccV1 $left) $right (ProofTraceNatSuccV1 $sum)) (HornBodyConsV1 (ProofTraceNatAddV1 $left $right $sum) HornBodyNilV1))
(HornClauseV1 (ProofTraceNatAddV1 ProofTraceNatZeroV1 $right $right) HornBodyNilV1)
(HornClauseV1 (ProofTraceNatCanonicalV1 $alias $canonical) (HornBodyConsV1 (source-proof-trace-nat-alias-v1 $alias $source) (HornBodyConsV1 (ProofTraceNatCanonicalV1 $source $canonical) HornBodyNilV1)))
(HornClauseV1 (ProofTraceNatCanonicalV1 (ProofTraceNatSuccV1 $source) (ProofTraceNatSuccV1 $canonical)) (HornBodyConsV1 (ProofTraceNatCanonicalV1 $source $canonical) HornBodyNilV1))
(HornClauseV1 (ProofTraceNatCanonicalV1 ProofTraceNatZeroV1 ProofTraceNatZeroV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceNatMulAddV1 $radix $value $digit $result) (HornBodyConsV1 (ProofTraceNatMulV1 $radix $value $scaled) (HornBodyConsV1 (ProofTraceNatAddV1 $scaled $digit $result) HornBodyNilV1)))
(HornClauseV1 (ProofTraceNatMulV1 (ProofTraceNatSuccV1 $left) $right $product) (HornBodyConsV1 (ProofTraceNatMulV1 $left $right $partial) (HornBodyConsV1 (ProofTraceNatAddV1 $right $partial $product) HornBodyNilV1)))
(HornClauseV1 (ProofTraceNatMulV1 ProofTraceNatZeroV1 $right ProofTraceNatZeroV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceSavePhaseTransitionV1 ProofTracePhaseJustCompletedV1 ProofTracePhaseBetweenV1) (HornBodyConsV1 (source-proof-trace-save-placement-v1 state-proof-save-immediately-after-use-v1) HornBodyNilV1))
(HornClauseV1 (ProofTraceSavePhaseTransitionV1 ProofTracePhaseJustCompletedV1 ProofTracePhaseJustCompletedV1) (HornBodyConsV1 (source-proof-trace-save-placement-v1 state-proof-save-repeatable-v1) HornBodyNilV1))
(HornClauseV1 (ProofTraceInputCompressedEvidenceV1 $request $initial-labels $explicit-labels $codes $target $trace) (HornBodyConsV1 (source-proof-trace-input-request-v1 $request $target) (HornBodyConsV1 (source-proof-trace-input-context-count-v1 $request $context-count) (HornBodyConsV1 (ProofTraceInputBuildContextV1 $request ProofTraceNatZeroV1 $context-count $context) (HornBodyConsV1 (ProofTraceCompileCompressedV1 $initial-labels $explicit-labels $codes $trace) (HornBodyConsV1 (ProofTraceAcceptedV1 $context $trace $target) HornBodyNilV1))))))
(HornClauseV1 (ProofTraceInputNormalEvidenceV1 $request $labels $target $trace) (HornBodyConsV1 (source-proof-trace-input-request-v1 $request $target) (HornBodyConsV1 (source-proof-trace-input-context-count-v1 $request $context-count) (HornBodyConsV1 (ProofTraceInputBuildContextV1 $request ProofTraceNatZeroV1 $context-count $context) (HornBodyConsV1 (ProofTraceCompileNormalV1 $labels $trace) (HornBodyConsV1 (ProofTraceAcceptedV1 $context $trace $target) HornBodyNilV1))))))
(HornClauseV1 (source-proof-trace-input-assertion-v1 input-identity-v1 input-wff-v1 (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-context-count-v1 input-request-v1 (ProofTraceNatSuccV1 (ProofTraceNatSuccV1 ProofTraceNatZeroV1))) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-context-v1 input-request-v1 (ProofTraceNatSuccV1 ProofTraceNatZeroV1) (ProofSequenceConsV1 input-wff-v1 (ProofSequenceConsV1 input-dummy-v1 ProofSequenceNilV1))) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-context-v1 input-request-v1 ProofTraceNatZeroV1 (ProofSequenceConsV1 input-wff-v1 (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-apartness-count-v1 input-identity-v1 ProofTraceNatZeroV1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-frame-count-v1 input-identity-v1 (ProofTraceNatSuccV1 ProofTraceNatZeroV1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-frame-v1 input-identity-v1 ProofTraceNatZeroV1 (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-label-different-v1 input-identity-v1 input-hx-v1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-incomplete-compressed-code-v1 63) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-incomplete-normal-label-v1 input-unknown-v1) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-push-label-v1 input-request-v1 input-hdummy-v1 (ProofSequenceConsV1 input-wff-v1 (ProofSequenceConsV1 input-dummy-v1 ProofSequenceNilV1))) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-push-label-v1 input-request-v1 input-hx-v1 (ProofSequenceConsV1 input-wff-v1 (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-request-v1 input-request-v1 (ProofSequenceConsV1 input-wff-v1 (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))) HornBodyNilV1)
(HornClauseV1 (source-proof-trace-input-token-variable-v1 input-x-v1) HornBodyNilV1)
(HornClauseV1 (ProofTraceInputAcceptIncompleteCompressedV1 $request $initial-labels $explicit-labels $codes) (HornBodyConsV1 (source-proof-trace-input-request-v1 $request $target) (HornBodyConsV1 (source-proof-trace-input-context-count-v1 $request $context-count) (HornBodyConsV1 (ProofTraceInputBuildContextV1 $request ProofTraceNatZeroV1 $context-count $context) (HornBodyConsV1 (ProofTraceCompileCompressedIncompleteAcceptedV1 $context $initial-labels $explicit-labels $codes $target) HornBodyNilV1)))))
(HornClauseV1 (ProofTraceInputAcceptIncompleteNormalV1 $request $labels) (HornBodyConsV1 (source-proof-trace-input-request-v1 $request $target) (HornBodyConsV1 (source-proof-trace-input-context-count-v1 $request $context-count) (HornBodyConsV1 (ProofTraceInputBuildContextV1 $request ProofTraceNatZeroV1 $context-count $context) (HornBodyConsV1 (ProofTraceCompileNormalIncompleteAcceptedV1 $context $labels $target) HornBodyNilV1)))))
(HornClauseV1 (source-proof-trace-label-instruction-v1 $label (ProofTraceApplyV1 $assertion $frame)) (HornBodyConsV1 (ProofTraceInputAssertionV1 $label $assertion $frame) HornBodyNilV1))
(HornClauseV1 (ProofAssertionDeclaredV1 $assertion) (HornBodyConsV1 (ProofTraceInputAssertionV1 $label $assertion $frame) HornBodyNilV1))
(HornClauseV1 (ProofTraceInputAssertionV1 $label (ProofAssertionV1 $variables $essentials $conclusion-type $conclusion-template $apartness) $frame) (HornBodyConsV1 (source-proof-trace-input-assertion-v1 $label $conclusion-type $conclusion-template) (HornBodyConsV1 (source-proof-trace-input-frame-count-v1 $label $frame-count) (HornBodyConsV1 (source-proof-trace-input-apartness-count-v1 $label $apartness-count) (HornBodyConsV1 (ProofTraceInputBuildFrameV1 $label ProofTraceNatZeroV1 $frame-count $frame) (HornBodyConsV1 (ProofTraceFrameProjectV1 $frame $variables $essentials) (HornBodyConsV1 (ProofTraceInputBuildApartnessV1 $label ProofTraceNatZeroV1 $apartness-count $apartness) HornBodyNilV1)))))))
(HornClauseV1 (ProofTraceInputBuildApartnessV1 $assertion $index $count (ProofAssertionListConsV1 (ProofAssertionDisjointV1 $left $right) $apartness)) (HornBodyConsV1 (source-proof-trace-input-apartness-v1 $assertion $index $left $right) (HornBodyConsV1 (ProofTraceInputBuildApartnessV1 $assertion (ProofTraceNatSuccV1 $index) $count $apartness) HornBodyNilV1)))
(HornClauseV1 (ProofTraceInputBuildApartnessV1 $assertion $count $count ProofAssertionListNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceInputBuildContextV1 $request $index $count (ProofTraceContextConsV1 $formula $context)) (HornBodyConsV1 (source-proof-trace-input-context-v1 $request $index $formula) (HornBodyConsV1 (ProofTraceInputBuildContextV1 $request (ProofTraceNatSuccV1 $index) $count $context) HornBodyNilV1)))
(HornClauseV1 (ProofTraceInputBuildContextV1 $request $count $count ProofTraceContextNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceInputBuildFrameV1 $assertion $index $count (ProofTraceFrameConsV1 $entry $frame)) (HornBodyConsV1 (source-proof-trace-input-frame-v1 $assertion $index $entry) (HornBodyConsV1 (ProofTraceInputBuildFrameV1 $assertion (ProofTraceNatSuccV1 $index) $count $frame) HornBodyNilV1)))
(HornClauseV1 (ProofTraceInputBuildFrameV1 $assertion $count $count ProofTraceFrameNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTokenApartV1 $left $right) (HornBodyConsV1 (source-proof-trace-input-current-apart-v1 $left $right) HornBodyNilV1))
(HornClauseV1 (ProofTokenApartV1 $right $left) (HornBodyConsV1 (source-proof-trace-input-current-apart-v1 $left $right) HornBodyNilV1))
(HornClauseV1 (source-proof-trace-label-instruction-v1 $label (ProofTracePushV1 $formula)) (HornBodyConsV1 (source-proof-trace-input-request-v1 $request $target) (HornBodyConsV1 (source-proof-trace-input-push-label-v1 $request $label $formula) HornBodyNilV1)))
(HornClauseV1 (ProofTokenDifferentV1 $left $right) (HornBodyConsV1 (source-proof-trace-input-token-different-v1 $left $right) HornBodyNilV1))
(HornClauseV1 (ProofTokenLiteralV1 $token) (HornBodyConsV1 (source-proof-trace-input-token-literal-v1 $token) HornBodyNilV1))
(HornClauseV1 (ProofTokenVariableV1 $token) (HornBodyConsV1 (source-proof-trace-input-token-variable-v1 $token) HornBodyNilV1))
(HornClauseV1 (ProofTraceInputVerifyCompressedV1 $request $initial-labels $explicit-labels $codes) (HornBodyConsV1 (source-proof-trace-input-request-v1 $request $target) (HornBodyConsV1 (source-proof-trace-input-context-count-v1 $request $context-count) (HornBodyConsV1 (ProofTraceInputBuildContextV1 $request ProofTraceNatZeroV1 $context-count $context) (HornBodyConsV1 (ProofTraceCompileCompressedAcceptedV1 $context $initial-labels $explicit-labels $codes $target) HornBodyNilV1)))))
(HornClauseV1 (ProofTraceInputVerifyNormalV1 $request $labels) (HornBodyConsV1 (source-proof-trace-input-request-v1 $request $target) (HornBodyConsV1 (source-proof-trace-input-context-count-v1 $request $context-count) (HornBodyConsV1 (ProofTraceInputBuildContextV1 $request ProofTraceNatZeroV1 $context-count $context) (HornBodyConsV1 (ProofTraceCompileNormalAcceptedV1 $context $labels $target) HornBodyNilV1)))))
(HornClauseV1 (ProofTraceAcceptedV1 $context $trace $target) (HornBodyConsV1 (ProofTraceExecV1 $context $trace ProofTraceStackNilV1 (ProofTraceStackConsV1 $target ProofTraceStackNilV1)) HornBodyNilV1))
(HornClauseV1 (ProofTraceApplyAssertionV1 (ProofAssertionV1 $variables $essentials $conclusion-type $conclusion-template $disjoints) $frame $input (ProofTraceStackConsV1 (ProofSequenceConsV1 $conclusion-type $result) $rest)) (HornBodyConsV1 (ProofAssertionDeclaredV1 (ProofAssertionV1 $variables $essentials $conclusion-type $conclusion-template $disjoints)) (HornBodyConsV1 (ProofTraceFrameProjectV1 $frame $variables $essentials) (HornBodyConsV1 (ProofTraceFrameApplyV1 $frame $input $images $actuals $rest) (HornBodyConsV1 (ProofTraceBuildEnvironmentV1 $variables $images $environment) (HornBodyConsV1 (ProofTraceCheckEssentialsV1 $essentials $actuals $environment) (HornBodyConsV1 (ProofAssertionCheckDisjointsV1 $disjoints $environment) (HornBodyConsV1 (ProofSequenceInstantiateV1 $conclusion-template $environment $result) HornBodyNilV1))))))))
(HornClauseV1 (ProofTraceBuildEnvironmentV1 (ProofAssertionListConsV1 (ProofAssertionVariableV1 $variable $typecode) $variables) (ProofAssertionListConsV1 $image $images) (ProofEnvironmentConsV1 $variable $image $environment)) (HornBodyConsV1 (ProofTokenVariableV1 $variable) (HornBodyConsV1 (ProofTraceBuildEnvironmentV1 $variables $images $environment) HornBodyNilV1)))
(HornClauseV1 (ProofTraceBuildEnvironmentV1 ProofAssertionListNilV1 ProofAssertionListNilV1 ProofEnvironmentNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceCheckEssentialsV1 (ProofAssertionListConsV1 (ProofAssertionEssentialV1 $template) $essentials) (ProofAssertionListConsV1 $actual $actuals) $environment) (HornBodyConsV1 (ProofSequenceInstantiateV1 $template $environment $actual) (HornBodyConsV1 (ProofTraceCheckEssentialsV1 $essentials $actuals $environment) HornBodyNilV1)))
(HornClauseV1 (ProofTraceCheckEssentialsV1 ProofAssertionListNilV1 ProofAssertionListNilV1 $environment) HornBodyNilV1)
(HornClauseV1 (ProofTraceContextMemberV1 (ProofTraceContextConsV1 $formula $rest) $formula) HornBodyNilV1)
(HornClauseV1 (ProofTraceContextMemberV1 (ProofTraceContextConsV1 $head $rest) $formula) (HornBodyConsV1 (ProofTraceContextMemberV1 $rest $formula) HornBodyNilV1))
(HornClauseV1 (ProofTraceExecStateV1 $context (ProofTraceConsV1 (ProofTraceApplyV1 $assertion $frame) $trace) $stack $saved $output $output-saved) (HornBodyConsV1 (ProofTraceApplyAssertionV1 $assertion $frame $stack $next-stack) (HornBodyConsV1 (ProofTraceExecStateV1 $context $trace $next-stack $saved $output $output-saved) HornBodyNilV1)))
(HornClauseV1 (ProofTraceExecStateV1 $context (ProofTraceConsV1 (ProofTraceIncompleteStepV1 $formula) $trace) $stack $saved $output $output-saved) (HornBodyConsV1 (ProofTraceExecStateV1 $context $trace (ProofTraceStackConsV1 $formula $stack) $saved $output $output-saved) HornBodyNilV1))
(HornClauseV1 (ProofTraceExecStateV1 $context ProofTraceNilV1 $stack $saved $stack $saved) HornBodyNilV1)
(HornClauseV1 (ProofTraceExecStateV1 $context (ProofTraceConsV1 (ProofTracePushV1 $formula) $trace) $stack $saved $output $output-saved) (HornBodyConsV1 (ProofTraceContextMemberV1 $context $formula) (HornBodyConsV1 (ProofTraceExecStateV1 $context $trace (ProofTraceStackConsV1 $formula $stack) $saved $output $output-saved) HornBodyNilV1)))
(HornClauseV1 (ProofTraceExecStateV1 $context (ProofTraceConsV1 (ProofTraceRestoreV1 $index) $trace) $stack $saved $output $output-saved) (HornBodyConsV1 (ProofTraceSavedLookupV1 $index $saved $formula) (HornBodyConsV1 (ProofTraceExecStateV1 $context $trace (ProofTraceStackConsV1 $formula $stack) $saved $output $output-saved) HornBodyNilV1)))
(HornClauseV1 (ProofTraceExecStateV1 $context (ProofTraceConsV1 ProofTraceSaveV1 $trace) (ProofTraceStackConsV1 $formula $stack) $saved $output $output-saved) (HornBodyConsV1 (ProofTraceSavedAppendV1 $saved $formula $next-saved) (HornBodyConsV1 (ProofTraceExecStateV1 $context $trace (ProofTraceStackConsV1 $formula $stack) $next-saved $output $output-saved) HornBodyNilV1)))
(HornClauseV1 (ProofTraceExecV1 $context $trace $input $output) (HornBodyConsV1 (ProofTraceExecStateV1 $context $trace $input ProofTraceSavedNilV1 $output $output-saved) HornBodyNilV1))
(HornClauseV1 (ProofTraceFrameApplyV1 (ProofTraceFrameConsV1 (ProofTraceFrameEssentialV1 $template) $rest) $stack $images (ProofAssertionListConsV1 $actual $actuals) $output) (HornBodyConsV1 (ProofTraceFrameApplyV1 $rest $stack $images $actuals (ProofTraceStackConsV1 $actual $output)) HornBodyNilV1))
(HornClauseV1 (ProofTraceFrameApplyV1 ProofTraceFrameNilV1 $stack ProofAssertionListNilV1 ProofAssertionListNilV1 $stack) HornBodyNilV1)
(HornClauseV1 (ProofTraceFrameApplyV1 (ProofTraceFrameConsV1 (ProofTraceFrameVariableV1 $variable $typecode) $rest) $stack (ProofAssertionListConsV1 $image $images) $actuals $output) (HornBodyConsV1 (ProofTraceFrameApplyV1 $rest $stack $images $actuals (ProofTraceStackConsV1 (ProofSequenceConsV1 $typecode $image) $output)) HornBodyNilV1))
(HornClauseV1 (ProofTraceFrameProjectV1 (ProofTraceFrameConsV1 (ProofTraceFrameEssentialV1 $template) $rest) $variables (ProofAssertionListConsV1 (ProofAssertionEssentialV1 $template) $essentials)) (HornBodyConsV1 (ProofTraceFrameProjectV1 $rest $variables $essentials) HornBodyNilV1))
(HornClauseV1 (ProofTraceFrameProjectV1 ProofTraceFrameNilV1 ProofAssertionListNilV1 ProofAssertionListNilV1) HornBodyNilV1)
(HornClauseV1 (ProofTraceFrameProjectV1 (ProofTraceFrameConsV1 (ProofTraceFrameVariableV1 $variable $typecode) $rest) (ProofAssertionListConsV1 (ProofAssertionVariableV1 $variable $typecode) $variables) $essentials) (HornBodyConsV1 (ProofTraceFrameProjectV1 $rest $variables $essentials) HornBodyNilV1))
(HornClauseV1 (ProofTraceSavedAppendV1 (ProofTraceSavedConsV1 $head $rest) $formula (ProofTraceSavedConsV1 $head $output)) (HornBodyConsV1 (ProofTraceSavedAppendV1 $rest $formula $output) HornBodyNilV1))
(HornClauseV1 (ProofTraceSavedAppendV1 ProofTraceSavedNilV1 $formula (ProofTraceSavedConsV1 $formula ProofTraceSavedNilV1)) HornBodyNilV1)
(HornClauseV1 (ProofTraceSavedLookupV1 (ProofTraceNatSuccV1 $index) (ProofTraceSavedConsV1 $head $rest) $formula) (HornBodyConsV1 (ProofTraceSavedLookupV1 $index $rest $formula) HornBodyNilV1))
(HornClauseV1 (ProofTraceSavedLookupV1 ProofTraceNatZeroV1 (ProofTraceSavedConsV1 $formula $rest) $formula) HornBodyNilV1)

!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputVerifyNormalV1
         input-request-v1
         (ProofTraceLabelConsV1 input-hx-v1
           (ProofTraceLabelConsV1
             input-identity-v1 ProofTraceLabelNilV1))))
     (MetamathProofTracePeTTaNormalV1 AcceptedV1)))

!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputVerifyCompressedV1
         input-request-v1
         (ProofTraceLabelConsV1 input-hx-v1 ProofTraceLabelNilV1)
         (ProofTraceLabelConsV1 input-identity-v1 ProofTraceLabelNilV1)
         (ProofTraceCodeConsV1 65
           (ProofTraceCodeConsV1 66 ProofTraceCodeNilV1))))
     (MetamathProofTracePeTTaCompressedV1 AcceptedV1)))

!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputNormalEvidenceV1
         input-request-v1
         (ProofTraceLabelConsV1 input-hx-v1
           (ProofTraceLabelConsV1
             input-identity-v1 ProofTraceLabelNilV1))
         (ProofSequenceConsV1 input-wff-v1
           (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))
         (ProofTraceConsV1
           (ProofTracePushV1
             (ProofSequenceConsV1 input-wff-v1
               (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)))
           (ProofTraceConsV1
             (ProofTraceApplyV1
               (ProofAssertionV1
                 (ProofAssertionListConsV1
                   (ProofAssertionVariableV1 input-x-v1 input-wff-v1)
                   ProofAssertionListNilV1)
                 ProofAssertionListNilV1
                 input-wff-v1
                 (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
                 ProofAssertionListNilV1)
               (ProofTraceFrameConsV1
                 (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)
                 ProofTraceFrameNilV1))
             ProofTraceNilV1))))
     (MetamathProofTracePeTTaNormalEvidenceV1
       (ProofSequenceConsV1 input-wff-v1
         (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))
       (ProofTraceConsV1
         (ProofTracePushV1
           (ProofSequenceConsV1 input-wff-v1
             (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)))
         (ProofTraceConsV1
           (ProofTraceApplyV1
             (ProofAssertionV1
               (ProofAssertionListConsV1
                 (ProofAssertionVariableV1 input-x-v1 input-wff-v1)
                 ProofAssertionListNilV1)
               ProofAssertionListNilV1
               input-wff-v1
               (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
               ProofAssertionListNilV1)
             (ProofTraceFrameConsV1
               (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)
               ProofTraceFrameNilV1))
           ProofTraceNilV1)))))

!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputCompressedEvidenceV1
         input-request-v1
         (ProofTraceLabelConsV1 input-hx-v1 ProofTraceLabelNilV1)
         (ProofTraceLabelConsV1 input-identity-v1 ProofTraceLabelNilV1)
         (ProofTraceCodeConsV1 65
           (ProofTraceCodeConsV1 66 ProofTraceCodeNilV1))
         (ProofSequenceConsV1 input-wff-v1
           (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))
         (ProofTraceConsV1
           (ProofTracePushV1
             (ProofSequenceConsV1 input-wff-v1
               (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)))
           (ProofTraceConsV1
             (ProofTraceApplyV1
               (ProofAssertionV1
                 (ProofAssertionListConsV1
                   (ProofAssertionVariableV1 input-x-v1 input-wff-v1)
                   ProofAssertionListNilV1)
                 ProofAssertionListNilV1
                 input-wff-v1
                 (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
                 ProofAssertionListNilV1)
               (ProofTraceFrameConsV1
                 (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)
                 ProofTraceFrameNilV1))
             ProofTraceNilV1))))
     (MetamathProofTracePeTTaCompressedEvidenceV1
       (ProofSequenceConsV1 input-wff-v1
         (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))
       (ProofTraceConsV1
         (ProofTracePushV1
           (ProofSequenceConsV1 input-wff-v1
             (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)))
         (ProofTraceConsV1
           (ProofTraceApplyV1
             (ProofAssertionV1
               (ProofAssertionListConsV1
                 (ProofAssertionVariableV1 input-x-v1 input-wff-v1)
                 ProofAssertionListNilV1)
               ProofAssertionListNilV1
               input-wff-v1
               (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
               ProofAssertionListNilV1)
             (ProofTraceFrameConsV1
               (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)
               ProofTraceFrameNilV1))
           ProofTraceNilV1)))))

!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputVerifyNormalV1
         input-request-v1
         (ProofTraceLabelConsV1
           input-hdummy-v1 ProofTraceLabelNilV1)))
     (MetamathProofTracePeTTaWrongTargetV1 UnexpectedV1)))
