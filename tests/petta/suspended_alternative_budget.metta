; Each iteration leaves two calls suspended behind it, one on each route: a
; closed call with an untried equation and an open call with untried rows.
(= (pick ()) a)
(= (pick $x) b)
(= (color red) warm)
(= (color blue) cool)
(= (closed-tick) (and True (case (pick ()) ((a True)))))
(= (open-tick) (and True (case (color $x) ((warm True)))))
(= (run $n)
   (if (== $n 0) done (let $_ (closed-tick) (let $_ (open-tick) (run (- $n 1))))))
!(run 300)
