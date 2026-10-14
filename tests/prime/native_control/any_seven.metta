; A recursive alternative is authored before the productive alternative.
(= (integers $n) (integers (+ $n 1)))
(= (integers $n) $n)
!(select 7 (integers 0))

; Normalization retains the return constructor while the generator runs.
(= (trees) (S (trees)))
(= (trees) Z)
!(select 7 (trees))

; Native strict and pattern consumers retain their return obligations.
(= (plus-ten) (+ 10 (integers 0)))
(= (boxes $n) (boxes (+ $n 1)))
(= (boxes $n) (Box $n))
(= (unbox (Box $n)) $n)
!(select 7 (plus-ten))
!(select 7 (unbox (boxes 0)))
