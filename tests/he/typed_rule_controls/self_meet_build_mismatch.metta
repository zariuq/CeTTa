; A head's own tail call meets its instance by the call's checks where the
; argument at the formal the range reads is the parameter there, a checked
; build over it at a field its constructor types as its range, or such a
; field of it; any other argument is met at run time.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: SL (List String))
(: NL (List Number))
(: build (-> Number (List $a) (List $a)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: top (-> Number (List String)))
(= (top $n) (build $n Nil))
(: topn (-> Number (List Number)))
(= (topn $n) (build $n Nil))
(: us (-> (List String) %Undefined%))
(= (us $l) (build 2 $l))
; A case evaluates its key where no type is expected of it: the error is
; the failing build check's own.
!(case (us SL) (((Error $call $reason) (failed-at $call)) ($v $v)))
