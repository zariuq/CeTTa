; Quoted code remains data even when it contains pattern variables and names
; an executable function. Structural inspection never runs that function.
(= (add Z $n) $n)
(= (add (S $m) $n) (S (add $m $n)))
(= (identity-data $x) $x)
(= (left-side $equation) (let (= $left $right) $equation $left))
!(let (quote $code) (quote (add Z $n)) (== $code ()))
!(let (quote $code) (quote (add Z $n)) (car-atom $code))
!(let (quote $code) (quote (add Z $n)) (identity-data $code))
!(let (quote $equation) (quote (= (add Z $n) $n)) (left-side $equation))
!(let (quote $code) (quote (add Z $n)) (unify $code (add Z value) $code unmatched))

; Explicit execution crosses the data/code boundary, including on open code.
!(let (quote $code) (quote (add Z $n)) (eval $code))
!(let (quote $code) (quote (add Z (S Z))) (eval $code))

; Code may describe an error or empty result without producing either.
!(let (quote $code) (quote (Error bad detail)) (get-metatype $code))
!(let (quote $code) (quote (Error bad detail)) (identity-data $code))
!(let (quote $code) (quote (Error bad detail)) (car-atom $code))
!(let (quote $code) (quote (Empty)) (identity-data $code))
