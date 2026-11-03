; `!` is the query marker, no head.  Written alone it takes no
; meta-argument, so a bracket or brace touching it opens the query's term,
; as a parenthesis does: `![...]` and `!{...}` read as `! [...]` and
; `! {...}`, and nothing is stored.
!{a b}
! {a b}
![a b c]
! [a b c]
!(+ 1 2)
!(collapse (match &self (meta $h $a) (stored-meta $h $a)))

; Text read by `parse` follows the same rule.
!(parse "![a b]")
!(parse "!(a b)")
!(parse "(g ![x])")

; Negative: a word that starts with `!` is a word, as HE's reader takes it,
; so `!c` is the stored symbol `!c`, not a query.
!c
!(collapse (match &self !c stored))
