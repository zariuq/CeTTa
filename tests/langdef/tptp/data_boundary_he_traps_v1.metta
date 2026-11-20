; The data boundary's trap on the host builtin `and`, for the HE lanes: HE
; lets a program define it, and a formula read as data must not fire it.

(= (and $x $y) BOOM-and)
