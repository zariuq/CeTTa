; Error is a data tag only when no actual callable owns this source head.
; SWI-PeTTa reference witness, exercised by the constructor gate.
(= (Error payload $reason) (list-value:handled $reason))
!(collapse (LCons (Error payload reason) LNil))
!(collapse (LCons (Error absent reason) LNil))
!(LCons (quote (Error payload reason)) LNil)
