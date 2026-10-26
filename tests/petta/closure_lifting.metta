; A relation that reads its closure with a variable, passes it on unchanged
; and never binds it again is derived once per closure shape: the closures
; below differ only in the data they carry.
(= (fold-flat $f $init ()) $init)
(= (fold-flat $f $init (cons $x $xs)) (fold-flat $f ($f $init $x) $xs))
(= (tally $backend $id $acc $x)
   (progn (add-atom &self (seen $backend $id $x)) (+ $acc (* $id $x))))
(= (tallies $n $acc)
   (if (== $n 0) $acc
       (let $s (fold-flat (tally live $n) 0 (cons 1 (cons 2 (cons 3 ()))))
         (tallies (- $n 1) (+ $acc $s)))))
!(fold-flat (tally live 1) 0 (cons 1 (cons 2 (cons 3 ()))))
!(fold-flat (tally live 2) 0 (cons 1 (cons 2 (cons 3 ()))))
!(fold-flat (tally temp 3) 0 (cons 1 (cons 2 ())))
!(tallies 40 0)
!(collapse (match &self (seen live 7 $x) $x))
; A closure carrying data that looks like code keeps it as data.
(= (pair-with $p $acc $x) (progn (add-atom &self (paired $p $x)) (+ $acc $x)))
!(fold-flat (pair-with (Pair a (+ 1 2))) 0 (cons 4 (cons 5 ())))
!(collapse (match &self (paired $p 5) $p))
; A closure carrying a closure: the inner one is part of the shape.
(= (twice $g $acc $x) ($g ($g $acc $x) $x))
!(fold-flat (twice (tally temp 2)) 1 (cons 3 ()))
; A closure the relation returns is rebuilt from what it carries.
(= (keep $f $x) (Kept $f ($f 0 $x)))
!(keep (tally temp 5) 7)
; A let that binds the closure parameter again is not lifted.
(= (again $f $x) (let $f (tally temp 9) ($f 0 $x)))
!(collapse (again (tally live 3) 2))
; Nor is a typed relation.
(: typed-fold (-> %Undefined% Number Expression Number))
(= (typed-fold $f $init ()) $init)
(= (typed-fold $f $init (cons $x $xs)) (typed-fold $f ($f $init $x) $xs))
!(collapse (typed-fold (tally live 11) 0 (cons 1 (cons 1 ()))))
; A lifted relation follows a redefinition of its source.
(= (walk-on $f ()) ())
(= (walk-on $f (cons $x $xs)) (cons ($f 0 $x) (walk-on $f $xs)))
!(walk-on (tally temp 4) (cons 1 ()))
!(add-atom &self (= (walk-on $f (cons $x $xs)) (Extra $x)))
!(collapse (walk-on (tally temp 4) (cons 1 ())))
