; A variable-only pattern does not demand its unused argument under Need.
; Eagerly evaluating the comparison would produce a type fault.
(= (prepared-keep $value $unused) $value)
!(prepared-keep 7 (== 1 "unused-fault"))
