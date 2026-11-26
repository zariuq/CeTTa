; A space expression of the read algebra, (, A B) or (| A B), is no space a
; write can reach: adding to it or removing from it raises the error a write
; to any operand that is no space raises, and the spaces stay as they were.
!(bind! &a (new-space))
!(bind! &b (new-space))
!(add-atom &a (x 1))
!(add-atom &b (x 1))
!(add-atom &b (y 2))
(= (put $s $x) (add-atom $s $x))
; Positive controls: writes to the spaces themselves land, through a
; function too.
!(add-atom &a (w 0))
!(remove-atom &a (w 0))
!(put &a (v 9))
!(remove-atom &a (v 9))
!(collapse (match &a $p $p))
; Negative controls: an operand that is no space.
!(catch (add-atom (foo bar) (z 3)))
!(catch (add-atom 42 (z 3)))
; Writes to views: flat, nested, a singleton fold, bound by let, through a
; function, and of an atom that is no expression.
!(catch (add-atom (, &a &b) (z 3)))
!(catch (remove-atom (, &a &b) (x 1)))
!(catch (add-atom (| &a &b) (z 3)))
!(catch (remove-atom (| &a &b) (y 2)))
!(catch (add-atom (, (| &a &b) &a) (z 3)))
!(catch (add-atom (, &a) (z 3)))
!(catch (let $v (, &a &b) (add-atom $v (q 5))))
!(catch (put (, &a &b) (z 3)))
!(catch (add-atom (, &a &b) 7))
; The spaces are as they were.
!(collapse (match &a $p $p))
!(collapse (match &b $p $p))
