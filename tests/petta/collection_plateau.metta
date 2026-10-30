; Turns whose terms are new every turn and die with it: a fact holding a
; fresh string and fresh expressions, added and removed again, and a note in
; a memory of eight slots that rotates.  What lives stays the same from turn
; to turn, so a universe that reclaims keeps its peak flat, whether the turns
; run as one evaluation or as several.
!(bind! &memory (new-space))
(= (turn $k)
   (let* (($payload (format-args "payload {} of {}" ($k (* $k 31))))
          ($fact (fresh $k $payload (pair $k (succ $k))))
          ($a (add-atom &memory $fact))
          ($r (remove-atom &memory $fact))
          ($slot (% $k 8))
          ($old (- $k 8))
          ($b (add-atom &memory (note $slot $k)))
          ($c (remove-atom &memory (note $slot $old))))
     done))
(= (turns $k $end) (if (>= $k $end) done (let $t (turn $k) (turns (+ $k 1) $end))))
!(turns 0 (argv 1))
!(turns (argv 1) (* 2 (argv 1)))
