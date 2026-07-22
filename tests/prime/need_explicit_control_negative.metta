; Untyped data is not known to be forceable.  Prime preserves the residual
; form instead of inventing either an application meaning or an error.
!(force ordinary-data)

; A grounded Number has a known non-function type, so this is a checked
; contradiction rather than an unknown application.
!(App 1 ignored)
