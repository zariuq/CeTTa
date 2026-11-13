; A bounded select observes a finite prefix: never more answers than its
; bound, however many threads evaluate the alternatives.  Which answers the
; prefix holds is open, so each query reads how many it took and which of them
; are not among the alternatives (none).
!(let $r (select 2 (hyperpose (a b c))) (selected (size-atom $r) (subtraction-atom $r (a b c))))
!(let $r (select 2 (hyperpose (a a a))) (selected (size-atom $r) (subtraction-atom $r (a a a))))
!(let $r (select 3 (hyperpose (a b c))) (selected (size-atom $r) (subtraction-atom $r (a b c))))
!(let $r (select 4 (hyperpose (a b c))) (selected (size-atom $r) (subtraction-atom $r (a b c))))
!(let $r (select 0 (hyperpose (a b c))) (selected (size-atom $r) (subtraction-atom $r (a b c))))
!(let $r (select 2 (hyperpose ((superpose (a b c)) d))) (selected (size-atom $r) (subtraction-atom $r (a b c d))))
