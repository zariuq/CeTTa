; A case pattern is syntax, but a Need cell in it is a value that branch
; consumes: the cell is forced when the selection reaches its branch, after
; the earlier branches failed, never before; each of its values continues the
; selection from that branch, and the scrutinee is evaluated once.
(= (k $x) (case 2 (($x hit) ($_ miss))))
(= (k2 $x $y) (case 3 (($x first) ($y second) ($_ none))))
(= (kp $x) (case (pair 2 b) (((pair $x $y) $y) ($_ none))))
(= (either) 1)
(= (either) 2)
(= (noisy $tag $v) (let $_ (println! $tag) $v))
(= (scrutinee) (let $_ (println! scrutinee) 3))
(= (ks $x $y) (case (scrutinee) (($x two) ($y three))))
; The cell (+ 1 1) is the value 2, also inside a structured pattern.
!(k (+ 1 1))
!(kp (+ 1 1))
; The second branch's cell is forced only after the first fails.
!(k2 (+ 1 1) (+ 1 2))
; A later branch's cell is not forced when an earlier branch matches: only x
; is printed.
!(k2 (noisy x 3) (noisy y 3))
; Forcing cells does not evaluate the scrutinee again: scrutinee is printed
; once though two branches force their cells.
!(ks (+ 1 1) (+ 1 2))
; One continuation per value of the cell.
!(k (either))
; A value needs no forcing.
!(k 2)
