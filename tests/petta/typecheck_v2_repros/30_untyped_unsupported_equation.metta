; With no declared or inferred signature, an unsupported recursive body has
; no static judgment.  Default mode preserves ordinary PeTTa execution;
; strict mode must reject the missing proof rather than silently succeeding.

(= (opaque-loop) (opaque-loop))
