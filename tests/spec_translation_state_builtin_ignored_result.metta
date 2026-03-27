; Builtin state surface with the change-state! result discarded.
;
; Positive example:
; - sequential state update followed by observation of get-state.
;
; Negative example:
; - does not observe the direct return value of change-state!.

!(bind! &count (new-state 0))
!(let $_ (change-state! &count 1)
   (get-state &count))
