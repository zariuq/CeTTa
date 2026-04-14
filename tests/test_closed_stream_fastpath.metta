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
