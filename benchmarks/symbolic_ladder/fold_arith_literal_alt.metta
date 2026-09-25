; A literal head is an alternative even though only the general head inlines.
(row a)
(= (literal-alt $i $a) (+ $i $a))
(= (literal-alt 1 $a) 99)
!(literal-alt 1 0)
!(foldall literal-alt (match &self (row $x) 1) 0)
