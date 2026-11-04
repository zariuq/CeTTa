; An authored superpose's items are solved as any goal's children are: a
; variable among them is the value bound to it, never evaluated again.  A
; quoted (bh-addk 7) is data, so (bh-addk 7) as an item, inside a
; constructor or as the head of an application stays data, in a lambda body,
; an equation body and a let, as SWI-PeTTa answers.
(= (bh-addk $k $x) (+ $x $k))
(= (apply1 $l $a) ($l $a))
(= (apply2 $l $a $b) ($l $a $b))
(= (in-equation $g) (apply1 (|-> ($f) (superpose (($f 3) 0))) $g))
(= (in-equation-2 $g $y) (apply2 (|-> ($f $x) (superpose (($f $x) 0))) $g $y))
(= (made $f) (|-> ($x) (superpose (($f $x) 0))))
(= (body-item $f) (superpose ($f 0)))
!((|-> ($f) (superpose ($f 0))) (quote (bh-addk 7)))
!((|-> ($f) (superpose ((Box $f) 0))) (quote (bh-addk 7)))
!((|-> ($f) (superpose (($f 3) 0))) (quote (bh-addk 7)))
!(apply1 (apply1 (|-> ($f) (|-> ($x) (superpose (($f $x) 0)))) (quote (bh-addk 7))) 3)
!(in-equation (quote (bh-addk 7)))
!(in-equation-2 (quote (bh-addk 7)) 3)
!(apply1 (made (quote (bh-addk 7))) 3)
!(body-item (quote (bh-addk 7)))
!(let $f (quote (bh-addk 7)) (superpose (($f 3) 0)))
; A function's value as an item is still a call's answer.
!((|-> ($f) (superpose (($f 3) 0))) (bh-addk 7))
!((|-> ($x) (superpose ($x (bh-addk 7 $x)))) 3)
