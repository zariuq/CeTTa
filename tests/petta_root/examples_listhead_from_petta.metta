; Ported from /home/zar/claude/hyperon/PeTTa/examples/listhead.metta
; Root-style Petta witness adapted to CeTTa's assertEqual surface.

(= (len ()) 0)
(= (len (cons $Head $Tail))
   (let $N0 (len $Tail)
        (+ $N0 1)))

!(assertEqual (let (cons $Head $Tail) (1 2 3 4 5 6) ($Head $Tail))
              (1 (2 3 4 5 6)))
!(assertEqual (len (1 2 3))
              3)
!(assertEqual (cons 42 ())
              (42))
