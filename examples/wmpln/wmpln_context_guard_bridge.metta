!(import! &self ../../lib/lib_wmpln)

; Runnable shadow of PLNContextGuardBridge.
;
; A context-complete, side-checked rule may extract evidence from the active
; state.  A context-missing or side-missing rule is blocked rather than
; manufacturing a surface value.

(= (guarded-extract $hasContext $evidence)
   (if $hasContext $evidence Blocked))

(= (guarded-fires $hasContext)
   $hasContext)

(= (guarded-rule-fires $hasContext $sideChecked)
   (and $hasContext $sideChecked))

(= (guarded-rule-itv $hasContext $sideChecked $evidence)
   (if (guarded-rule-fires $hasContext $sideChecked) $evidence Blocked))

(= (union-context-fires $hasLeft $hasRight $sideChecked)
   (and (and $hasLeft $hasRight) $sideChecked))

(= (union-context-revised-evidence $hasLeft $hasRight $sideChecked $left $right)
   (if (union-context-fires $hasLeft $hasRight $sideChecked)
       (+ $left $right)
       Blocked))

(= (query-union-context-fires $hasLeft $hasRight)
   (and $hasLeft $hasRight))

(= (query-union-revised-evidence $hasLeft $hasRight $left $right)
   (if (query-union-context-fires $hasLeft $hasRight)
       (+ $left $right)
       Blocked))

(= (widened-context-fires $hasRequired $hasExtra $sideChecked)
   (and $hasRequired $sideChecked))

(= (widened-context-evidence $hasRequired $hasExtra $sideChecked $requiredEvidence)
   (if (widened-context-fires $hasRequired $hasExtra $sideChecked)
       $requiredEvidence
       Blocked))

(= (threshold-rule-fires $hasContext $sideChecked $strength $tau)
   (and (guarded-rule-fires $hasContext $sideChecked)
        (<= $tau $strength)))

(= (threshold-rule-result $hasContext $sideChecked $strength $tau)
   (if (threshold-rule-fires $hasContext $sideChecked $strength $tau)
       ThresholdHolds
       Blocked))

(= (itv-coordinate-threshold-fires $hasContext $sideChecked $coordValue $tau)
   (threshold-rule-fires $hasContext $sideChecked $coordValue $tau))

(= (itv-coordinate-threshold-result $hasContext $sideChecked $coordValue $tau)
   (if (itv-coordinate-threshold-fires $hasContext $sideChecked $coordValue $tau)
       ITVThresholdHolds
       Blocked))

(= (required-context-fires $requiredPresent $sideChecked)
   (and $requiredPresent $sideChecked))

(= (required-context-result $requiredPresent $sideChecked $evidence)
   (if (required-context-fires $requiredPresent $sideChecked)
       $evidence
       Blocked))

; Positive: source state is available in the context, so the query fires.
!(assertEqualToResult (guarded-fires True) (True))
!(assertEqualToResult (guarded-extract True 0.8) (0.8))
!(assertEqualToResult (guarded-rule-fires True True) (True))
!(assertEqualToResult (guarded-rule-itv True True 0.8) (0.8))

; Positive: two separately available context sources compose by revision under
; the union context before the guarded rule fires.
!(assertEqualToResult (union-context-fires True True True) (True))
!(assertEqualToResult (union-context-revised-evidence True True True 3.0 1.0) (4.0))
!(assertEqualToResult (query-union-context-fires True True) (True))
!(assertEqualToResult (query-union-revised-evidence True True 3.0 1.0) (4.0))

; Positive: widening a context with irrelevant extra evidence preserves an
; already-fired judgment and its evidence value.
!(assertEqualToResult (widened-context-fires True True True) (True))
!(assertEqualToResult (widened-context-evidence True True True 0.8) (0.8))
!(assertEqualToResult (widened-context-evidence True False True 0.8) (0.8))

; Positive: strength-threshold rule truth is proof-carrying only when context,
; side condition, and the numeric threshold guard all hold.
!(assertEqualToResult (threshold-rule-fires True True 0.8 0.7) (True))
!(assertEqualToResult (threshold-rule-result True True 0.8 0.7) (ThresholdHolds))
!(assertEqualToResult (itv-coordinate-threshold-fires True True 0.35 0.25) (True))
!(assertEqualToResult (itv-coordinate-threshold-result True True 0.35 0.25) (ITVThresholdHolds))
!(assertEqualToResult (required-context-fires True True) (True))
!(assertEqualToResult (required-context-result True True 0.8) (0.8))

; Negative: missing context blocks the query.  `Blocked` is an executable
; witness of non-derivability, not a numeric truth value.
!(assertEqualToResult (guarded-fires False) (False))
!(assertEqualToResult (guarded-extract False 0.8) (Blocked))
!(assertEqualToResult (guarded-rule-fires False True) (False))
!(assertEqualToResult (guarded-rule-itv False True 0.8) (Blocked))
!(assertEqualToResult (union-context-fires True False True) (False))
!(assertEqualToResult (union-context-revised-evidence True False True 3.0 1.0) (Blocked))
!(assertEqualToResult (query-union-context-fires True False) (False))
!(assertEqualToResult (query-union-revised-evidence True False 3.0 1.0) (Blocked))
!(assertEqualToResult (query-union-context-fires False True) (False))
!(assertEqualToResult (query-union-revised-evidence False True 3.0 1.0) (Blocked))
!(assertEqualToResult (widened-context-fires False True True) (False))
!(assertEqualToResult (widened-context-evidence False True True 0.8) (Blocked))
!(assertEqualToResult (threshold-rule-fires False True 0.8 0.7) (False))
!(assertEqualToResult (threshold-rule-result False True 0.8 0.7) (Blocked))
!(assertEqualToResult (itv-coordinate-threshold-fires False True 0.35 0.25) (False))
!(assertEqualToResult (itv-coordinate-threshold-result False True 0.35 0.25) (Blocked))
!(assertEqualToResult (required-context-fires False True) (False))
!(assertEqualToResult (required-context-result False True 0.8) (Blocked))

; Negative: even with context, an unchecked side condition blocks the rule.
!(assertEqualToResult (guarded-rule-fires True False) (False))
!(assertEqualToResult (guarded-rule-itv True False 0.8) (Blocked))
!(assertEqualToResult (union-context-fires True True False) (False))
!(assertEqualToResult (union-context-revised-evidence True True False 3.0 1.0) (Blocked))
!(assertEqualToResult (widened-context-fires True True False) (False))
!(assertEqualToResult (widened-context-evidence True True False 0.8) (Blocked))
!(assertEqualToResult (threshold-rule-fires True False 0.8 0.7) (False))
!(assertEqualToResult (threshold-rule-result True False 0.8 0.7) (Blocked))
!(assertEqualToResult (itv-coordinate-threshold-fires True False 0.35 0.25) (False))
!(assertEqualToResult (itv-coordinate-threshold-result True False 0.35 0.25) (Blocked))
!(assertEqualToResult (required-context-fires True False) (False))
!(assertEqualToResult (required-context-result True False 0.8) (Blocked))

; Negative: context and side condition are not enough if the derived strength
; does not meet the threshold guard.
!(assertEqualToResult (threshold-rule-fires True True 0.6 0.7) (False))
!(assertEqualToResult (threshold-rule-result True True 0.6 0.7) (Blocked))
!(assertEqualToResult (itv-coordinate-threshold-fires True True 0.2 0.25) (False))
!(assertEqualToResult (itv-coordinate-threshold-result True True 0.2 0.25) (Blocked))
