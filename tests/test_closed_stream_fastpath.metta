!(import! &self list)

!(println! (collapse (let $xs (eval (list:range 5))
   (let $i (superpose $xs) done))))

!(println! (collapse (let $xs (eval (list:range 5))
   (let $i (superpose $xs) $i))))

!(println! (collapse (let $xs (eval (list:range 5))
   (let $i (superpose $xs) (+ $i 1)))))

!(println! (collect (superpose (1 2 3))))
!(println! (select 2 (superpose (1 2 3))))
!(println! (once (superpose (1 2 3))))
!(println! (collapse (superpose (a Empty b))))
!(println! (collapse (superpose ((superpose (x y)) Empty z))))
!(println! (collapse (superpose ((Error collapse-probe fail) ok done))))
!(println! (collapse (let $i (superpose ((Error stream-probe fail) a b)) done)))
!(println! (collapse (let $i (superpose ((Error stream-probe fail) a b)) $i)))
!(println! (collapse (let $i (superpose ((Error stream-probe fail))) done)))
!(println! (collapse (let $i (superpose (1 2)) (superpose (p q)))))
!(println! (collapse (superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose (deep))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
