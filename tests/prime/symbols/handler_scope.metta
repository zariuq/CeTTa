(set:profile hol)

; A shared producer forced inside and outside a measuring handler.  The
; handler, cost:firings, owns the productions it forces; those it forced
; before its values parted are kept by its continuation.  Forced inside
; first, the production is charged inside and reused outside; forced outside
; first, the handler finds it made and charges nothing.  Either way the whole
; run charges it once: nothing is lost and nothing is charged twice.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &self two nat (= two (succ (succ zero))))
!(P (cost:firings two) two)
!(cost:firings (P (cost:firings two) two))
!(P two (cost:firings two))
!(cost:firings (P two (cost:firings two)))
; A production that one value alone depends on stays with that value: each
; branch of the handler made its own choice, so the continuation chooses
; again, and both of its answers are kept.
(= coin heads)
(= coin tails)
!(P (cost:firings coin) coin)
