; Runtime add/remove plus raw match bodies should support the evalCustom pattern
; from upstream eval.metta.

(= (f $L $a $b)
   (let $result (+ $a $b)
        (append ($result) $L)))

(= (evalCustom $body)
   (let* (($a   (add-atom &self (= (myfunc) $body)))
          ($res (reduce (myfunc)))
          ($r   (remove-atom &self (= (myfunc) $body))))
         $res))

!(test (evalCustom (match &self (= (f (42) 40.7 2) $x) $x))
       (42.7 42))
