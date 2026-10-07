; One nullary producer with two alternatives, used twice.  A symbol names one
; production in a branch, so the uses share one choice: heads-heads and
; tails-tails, never a mixed pair.  This holds for two occurrences of the
; name, for a function using its argument twice, and for a binding.
(= coin heads)
(= coin tails)
(= (pairc $x) (P $x $x))
!(P coin coin)
!(pairc coin)
!(let $x coin (P $x coin))
!(collapse (P coin coin))
; Side by side, a call: an operation made again at each occurrence, shared
; when a function receives it once.
(= (flip) heads)
(= (flip) tails)
!(P (flip) (flip))
!(pairc (flip))
; Resampling evaluates afresh at every force, the symbol's production
; included; delaying shares.
!(let $r (resample coin) (P (force $r) (force $r)))
!(let $r (delay coin) (P (force $r) (force $r)))
; Two uses of a symbol with values 5 and 6 add up to 10 or 12.
(= c 5)
(= c 6)
!(+ c c)
