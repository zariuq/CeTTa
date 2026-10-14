(hol:profile core)

; A rule admitted without changing the rule space.  `two` first has a raw
; rule; `hol:define` then admits the same rule, adding evidence and no second
; copy.  The operational contributions are the same before and after: the
; values, their multiplicity and the shared choice.  The firing count is a
; measurement of declared definitions, so it counts the admitted rule and
; not the raw one.
!(hol:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
(= two (succ (succ zero)))
(= (pairc $x) (P $x $x))
!(collapse (P two two))
!(pairc two)
!(cost:firings (P two two))
!(hol:define &self two nat (= two (succ (succ zero))))
!(collapse (P two two))
!(pairc two)
!(cost:firings (P two two))
!(collapse (match &self (= two $v) $v))
; After admission the same constant has its three faces: it runs, the kernel
; unfolds it when it compares terms, and it is the set it names.
!(type:eq two (succ (succ zero)))
!(hol:eq two (succ (succ zero)))
