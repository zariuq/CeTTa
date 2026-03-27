; Shared builtin state surface:
; new-state, get-state, change-state!.
;
; Positive example:
; - both runtimes expose these heads directly.
;
; Negative example:
; - this does not yet prove the bridge to named-space semantics.

!(bind! &count (new-state 0))
!(get-state &count)
!(change-state! &count 1)
!(get-state &count)
