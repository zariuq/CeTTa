; Ambiguous remainder parser. Each leading d may be consumed or left
; in the remainder. Fully consumed input is one answer; shorter prefixes
; keep an open tail. Count 32.

(= (num (d $rest))
   (let $r (num $rest) (more $r)))
(= (num $rest) (stop $rest))

!(num (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d end)))))))))))))))))))))))))))))))))
!(let (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (more (stop end))))))))))))))))))))))))))))))))) (num (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d (d end))))))))))))))))))))))))))))))))) bound)
!(let (stop end) (num (d (d (d mismatch)))) miss)
!(num end)
