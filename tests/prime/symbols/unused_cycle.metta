(set:profile hol)

; A symbol the program defines is produced only when something demands it.
; A cyclic symbol handed to a function that never uses that argument is never
; unfolded, and neither is a symbol whose unfolding demands itself before it
; produces anything: the call answers at once.
(= ones (Cons 1 ones))
(= a b)
(= b a)
(= (k $x $y) $x)
!(k 1 ones)
!(k 1 a)
; Demanded, the unproductive cycle reports its condition: the production of
; `a` demanded `a` again before reaching a value.  It does not run until the
; stack ends.
! a
; With an admitted constant the unfolding is counted as a firing: none for
; the argument k discards, one for the argument it returns.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &self two nat (= two (succ (succ zero))))
!(cost:firings (k zero two))
!(cost:firings (k two zero))
