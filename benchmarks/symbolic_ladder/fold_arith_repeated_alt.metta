; Repeated variables in another head can also produce a second result.
(row a)
(= (repeated-alt $i $a) (+ $i $a))
(= (repeated-alt $v $v) 99)
!(repeated-alt 1 1)
!(foldall repeated-alt (match &self (row $x) 1) 1)
