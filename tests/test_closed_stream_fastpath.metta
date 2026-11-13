!(import! &self list)

!(println! (collapse (let $xs (list:range 5)
   (let $i (superpose $xs) done))))

!(println! (let $answers (collapse (let $xs (list:range 5)
   (let $i (superpose $xs) $i))) (sort-atom $answers)))

!(println! (let $answers (collapse (let $xs (list:range 5)
   (let $i (superpose $xs) (+ $i 1)))) (sort-atom $answers)))

!(println! (let $answers (collect (superpose (1 2 3))) (sort-atom $answers)))
!(println! (let $two (select 2 (superpose (1 2 3))) (size-atom $two)))
!(println! (case (once (superpose (1 2 3))) ((1 one-of-them) (2 one-of-them) (3 one-of-them))))
!(println! (let $answers (collapse (superpose (a Empty b))) (sort-atom $answers)))
!(println! (let $answers (collapse (superpose ((superpose (x y)) Empty z))) (sort-atom $answers)))
!(println! (let $answers (collapse (superpose ((Error collapse-probe fail) ok done))) (sort-atom $answers)))
!(println! (collapse (let $i (superpose ((Error stream-probe fail) a b)) done)))
!(println! (let $answers (collapse (let $i (superpose ((Error stream-probe fail) a b)) $i)) (sort-atom $answers)))
!(println! (collapse (let $i (superpose ((Error stream-probe fail))) done)))
!(println! (let $answers (collapse (let $i (superpose (1 2)) (superpose (p q)))) (sort-atom $answers)))
!(println! (collapse (superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose ((superpose (deep))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
