; Parenthesis words. (lp (rp end)) is balanced and leaves (mark end).
; (lp (rp (lp end))) is a failed branch and prints nothing.
; The following query still answers.

(= (word end) (mark end))
(= (word (rp $rest)) (mark rp $rest))
(= (word (lp $more))
   (let (mark rp $out) (word $more) (word $out)))

!(let $r (word (lp (rp end))) (one $r))
!(let $r (word end) (two $r))
!(let $r (word (lp (rp (lp end)))) (three $r))
!(let $r (word (lp (lp (rp (rp end))))) (four $r))
