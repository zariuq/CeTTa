(set:profile hol)

; A shared production is charged once per shared occurrence.  The grade is
; the firing count of declared definitions (cost:firings); `two` is admitted,
; so producing it is one firing, however often it is used.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &self two nat (= two (succ (succ zero))))
!(set:define &self dbl (-> nat nat) (= (dbl zero) zero) (= (dbl (succ $n)) (succ (succ (dbl $n)))))
(= (pairc $x) (P $x $x))
!(cost:firings two)
!(cost:firings (P two two))
!(cost:firings (pairc two))
; Two calls are two operations, each charged; `two` inside them is produced
; once.
!(cost:firings (P (dbl two) (dbl two)))
; A symbol with two alternatives that cost firings of their own (1 and 4):
; used twice, each branch pays for its production once, the same total as a
; single use.  Resampled, every use pays again.
(= pick (dbl zero))
(= pick (dbl two))
!(cost:firings pick)
!(cost:firings (P pick pick))
!(cost:firings (let $r (resample pick) (P (force $r) (force $r))))
