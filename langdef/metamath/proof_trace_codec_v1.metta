; Authored byte classification for the Metamath compressed-proof code.  The
; generic trace compiler consumes these facts; native code does not interpret
; byte ranges or arithmetic constants.

(gslt-presentation-v1 MetamathProofTraceCodecV1
  (signature)
  (equations)
  (rewrites
    (rule mm-proof-trace-nat-one-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitOneV1
          (ProofTraceNatSuccV1 ProofTraceNatZeroV1)))
      (body))
    (rule mm-proof-trace-nat-two-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitTwoV1
          (ProofTraceNatSuccV1 ProofTraceDigitOneV1)))
      (body))
    (rule mm-proof-trace-nat-three-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitThreeV1
          (ProofTraceNatSuccV1 ProofTraceDigitTwoV1)))
      (body))
    (rule mm-proof-trace-nat-four-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitFourV1
          (ProofTraceNatSuccV1 ProofTraceDigitThreeV1)))
      (body))
    (rule mm-proof-trace-nat-five-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitFiveV1
          (ProofTraceNatSuccV1 ProofTraceDigitFourV1)))
      (body))
    (rule mm-proof-trace-nat-six-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitSixV1
          (ProofTraceNatSuccV1 ProofTraceDigitFiveV1)))
      (body))
    (rule mm-proof-trace-nat-seven-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitSevenV1
          (ProofTraceNatSuccV1 ProofTraceDigitSixV1)))
      (body))
    (rule mm-proof-trace-nat-eight-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitEightV1
          (ProofTraceNatSuccV1 ProofTraceDigitSevenV1)))
      (body))
    (rule mm-proof-trace-nat-nine-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitNineV1
          (ProofTraceNatSuccV1 ProofTraceDigitEightV1)))
      (body))
    (rule mm-proof-trace-nat-ten-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitTenV1
          (ProofTraceNatSuccV1 ProofTraceDigitNineV1)))
      (body))
    (rule mm-proof-trace-nat-eleven-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitElevenV1
          (ProofTraceNatSuccV1 ProofTraceDigitTenV1)))
      (body))
    (rule mm-proof-trace-nat-twelve-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitTwelveV1
          (ProofTraceNatSuccV1 ProofTraceDigitElevenV1)))
      (body))
    (rule mm-proof-trace-nat-thirteen-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitThirteenV1
          (ProofTraceNatSuccV1 ProofTraceDigitTwelveV1)))
      (body))
    (rule mm-proof-trace-nat-fourteen-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitFourteenV1
          (ProofTraceNatSuccV1 ProofTraceDigitThirteenV1)))
      (body))
    (rule mm-proof-trace-nat-fifteen-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitFifteenV1
          (ProofTraceNatSuccV1 ProofTraceDigitFourteenV1)))
      (body))
    (rule mm-proof-trace-nat-sixteen-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitSixteenV1
          (ProofTraceNatSuccV1 ProofTraceDigitFifteenV1)))
      (body))
    (rule mm-proof-trace-nat-seventeen-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitSeventeenV1
          (ProofTraceNatSuccV1 ProofTraceDigitSixteenV1)))
      (body))
    (rule mm-proof-trace-nat-eighteen-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitEighteenV1
          (ProofTraceNatSuccV1 ProofTraceDigitSeventeenV1)))
      (body))
    (rule mm-proof-trace-nat-nineteen-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitNineteenV1
          (ProofTraceNatSuccV1 ProofTraceDigitEighteenV1)))
      (body))
    (rule mm-proof-trace-nat-twenty-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceDigitTwentyV1
          (ProofTraceNatSuccV1 ProofTraceDigitNineteenV1)))
      (body))
    (rule mm-proof-trace-radix-five-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceRadixFiveV1 ProofTraceDigitFiveV1))
      (body))
    (rule mm-proof-trace-radix-twenty-v1
      (head
        (source-proof-trace-nat-alias-v1
          ProofTraceRadixTwentyV1 ProofTraceDigitTwentyV1))
      (body))

    (rule mm-proof-trace-terminal-a-v1
      (head (source-proof-trace-terminal-code-v1 65 ProofTraceDigitOneV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-b-v1
      (head (source-proof-trace-terminal-code-v1 66 ProofTraceDigitTwoV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-c-v1
      (head (source-proof-trace-terminal-code-v1 67 ProofTraceDigitThreeV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-d-v1
      (head (source-proof-trace-terminal-code-v1 68 ProofTraceDigitFourV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-e-v1
      (head (source-proof-trace-terminal-code-v1 69 ProofTraceDigitFiveV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-f-v1
      (head (source-proof-trace-terminal-code-v1 70 ProofTraceDigitSixV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-g-v1
      (head (source-proof-trace-terminal-code-v1 71 ProofTraceDigitSevenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-h-v1
      (head (source-proof-trace-terminal-code-v1 72 ProofTraceDigitEightV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-i-v1
      (head (source-proof-trace-terminal-code-v1 73 ProofTraceDigitNineV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-j-v1
      (head (source-proof-trace-terminal-code-v1 74 ProofTraceDigitTenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-k-v1
      (head (source-proof-trace-terminal-code-v1 75 ProofTraceDigitElevenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-l-v1
      (head (source-proof-trace-terminal-code-v1 76 ProofTraceDigitTwelveV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-m-v1
      (head (source-proof-trace-terminal-code-v1 77 ProofTraceDigitThirteenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-n-v1
      (head (source-proof-trace-terminal-code-v1 78 ProofTraceDigitFourteenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-o-v1
      (head (source-proof-trace-terminal-code-v1 79 ProofTraceDigitFifteenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-p-v1
      (head (source-proof-trace-terminal-code-v1 80 ProofTraceDigitSixteenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-q-v1
      (head (source-proof-trace-terminal-code-v1 81 ProofTraceDigitSeventeenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-r-v1
      (head (source-proof-trace-terminal-code-v1 82 ProofTraceDigitEighteenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-s-v1
      (head (source-proof-trace-terminal-code-v1 83 ProofTraceDigitNineteenV1 ProofTraceRadixTwentyV1))
      (body))
    (rule mm-proof-trace-terminal-t-v1
      (head (source-proof-trace-terminal-code-v1 84 ProofTraceDigitTwentyV1 ProofTraceRadixTwentyV1))
      (body))

    (rule mm-proof-trace-continuation-u-v1
      (head (source-proof-trace-continuation-code-v1 85 ProofTraceDigitOneV1 ProofTraceRadixFiveV1))
      (body))
    (rule mm-proof-trace-continuation-v-v1
      (head (source-proof-trace-continuation-code-v1 86 ProofTraceDigitTwoV1 ProofTraceRadixFiveV1))
      (body))
    (rule mm-proof-trace-continuation-w-v1
      (head (source-proof-trace-continuation-code-v1 87 ProofTraceDigitThreeV1 ProofTraceRadixFiveV1))
      (body))
    (rule mm-proof-trace-continuation-x-v1
      (head (source-proof-trace-continuation-code-v1 88 ProofTraceDigitFourV1 ProofTraceRadixFiveV1))
      (body))
    (rule mm-proof-trace-continuation-y-v1
      (head (source-proof-trace-continuation-code-v1 89 ProofTraceDigitFiveV1 ProofTraceRadixFiveV1))
      (body))
    (rule mm-proof-trace-save-z-v1
      (head (source-proof-trace-save-code-v1 90))
      (body))
    (rule mm-proof-trace-accept-incomplete-normal-v1
      (head (source-proof-trace-accept-incomplete-normal-byte-v1 63))
      (body))
    (rule mm-proof-trace-accept-incomplete-compressed-v1
      (head (source-proof-trace-accept-incomplete-compressed-byte-v1 63))
      (body))
  ))
