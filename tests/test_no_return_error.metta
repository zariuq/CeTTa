; NoReturn error — function block without return
; Ref: minimal-metta.md lines 134-136
; Note: (function (chain (+ 1 2) $x $x)) evaluates body to 3,
; which is not (return ...), so should produce NoReturn error.
!(function (chain (+ 1 2) $x $x))
