!(import! &self (library lib_tensor))

; A packed tensor, a view and one bulk operation. No Python is required.
!(let $matrix (tensor:from f64 (2 3) (1 2 3 4 5 6))
   (tensor:sum (tensor:reshape $matrix (6))))

; Updates preserve the original, including across alternatives.
!(let $original (tensor:from i64 (3) (10 20 30))
   (collapse (tensor:to-expr
     (superpose ((tensor:with $original 1 99) $original)))))

; Fill and reduce without constructing an Atom for each entry.
!(tensor:sum (tensor:full i64 (100000) 1))
