; A head declared Expression can hold a partial application, so applying it
; is a dynamic call whose Number result is checked when it runs.
(: apply-plus-one (-> Expression Number Number))
(= (apply-plus-one $f $x) (+ ($f $x) 1))

!(test (apply-plus-one (+ 1) 2) 4)
