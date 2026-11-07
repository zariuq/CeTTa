; Quasigroup completion by plain search, an order-14 instance with 118 empty
; squares.  A cell (R C (V ...)) is an empty square and the values its row
; and column leave open; the search fills the cell with fewest values,
; removes that value from its row and column, fails a branch that leaves a
; cell with none, and recurses.
;
; The search backtracks below activations whose slots were stored while no
; newer frame was live, so those stores carry no trail entry, after a
; collection had promoted the activations.  A later minor collection of the
; open equation machine must not trace such a dead slot: what it holds was
; freed by the backtrack.

(= (drop $v $xs)
   (if (== $xs ()) ()
     (let ($x $t) (decons-atom $xs)
       (let $rest (drop $v $t)
         (if (== $x $v) $rest (cons-atom $x $rest))))))

(= (fc $r $c $v $cells)
   (if (== $cells ()) ()
     (let ($cell $t) (decons-atom $cells)
       (let ($r2 $c2 $dom) $cell
         (let $rest (fc $r $c $v $t)
           (if (== $r $r2)
               (let $d (drop $v $dom)
                 (if (== $d ()) (empty) (cons-atom ($r2 $c2 $d) $rest)))
               (if (== $c $c2)
                   (let $d (drop $v $dom)
                     (if (== $d ()) (empty) (cons-atom ($r2 $c2 $d) $rest)))
                   (cons-atom $cell $rest))))))))

(= (pick-min $cells)
   (let ($cell $t) (decons-atom $cells)
     (if (== $t ()) ($cell ())
       (let ($b $r) (pick-min $t)
         (let ($r1 $c1 $d1) $cell
           (let ($rb $cb $db) $b
             (if (< (size-atom $d1) (size-atom $db))
                 ($cell $t)
                 ($b (cons-atom $cell $r)))))))))

(= (elem $xs) (let ($x $t) (decons-atom $xs) $x))
(= (elem $xs) (let ($x $t) (decons-atom $xs) (elem $t)))

(= (open $cells) True)

(= (solve $cells $n)
   (if (== $cells ()) done
     (let ($cell $rest) (pick-min $cells)
       (let ($r $c $dom) $cell
         (let $v (elem $dom)
           (let $next (fc $r $c $v $rest)
             (if (open $next) (solve $next $n) (empty))))))))

!(collapse (once (solve ((1 2 (2 3 5 12)) (1 3 (3 10 12)) (1 6 (1 3 4 5 12)) (1 8 (2 3 5 11)) (1 9 (1 2 3 4 5 11 12)) (1 10 (1 2 4 5 11 12)) (1 12 (3 4 5 10 11)) (1 14 (3 4 5 10 12)) (2 3 (3 6 7 8 9 12 14)) (2 4 (2 3 7 8 9 12)) (2 6 (3 7 9 12 14)) (2 9 (2 3 6 8 9 12)) (2 11 (6 7 12)) (2 12 (3 6 8 9 14)) (2 13 (2 3 6 7 8 14)) (2 14 (3 6 7 12 14)) (3 3 (3 6 8 13)) (3 4 (2 3 4 5 8 11 13)) (3 6 (3 4 5)) (3 7 (5 6 11)) (3 10 (2 4 5 6 8 11)) (3 11 (5 6 11)) (3 13 (2 3 4 5 6 8 11)) (3 14 (3 4 5 6 13)) (4 2 (3 5 8 12)) (4 5 (3 8 9 10 12)) (4 7 (5 9 12 14)) (4 8 (3 5 8)) (4 11 (5 10 12)) (4 12 (3 4 5 8 9 10 14)) (4 13 (3 4 5 8 10 14)) (4 14 (3 4 5 10 12 14)) (5 3 (13 14)) (5 4 (2 4 5 11 13)) (5 5 (11 13)) (5 9 (2 4 5 11)) (5 11 (5 11)) (5 13 (2 4 5 11 14)) (6 1 (4 8 9 11 14)) (6 2 (2 3 5 7 8 12)) (6 3 (3 6 7 8 9 12 14)) (6 5 (1 3 6 8 9 11 12)) (6 6 (1 3 4 5 7 9 12 14)) (6 7 (5 6 7 9 11 12 14)) (6 8 (2 3 5 7 8 11)) (6 9 (1 2 3 4 5 6 8 9 11 12)) (6 10 (1 2 4 5 6 8 9 11 12 14)) (6 12 (3 4 5 6 8 9 11 14)) (6 13 (1 2 3 4 5 6 7 8 11 14)) (6 14 (3 4 5 6 7 12 14)) (7 1 (8 9 10 11 14)) (7 3 (6 7 8 9 10 12 13 14)) (7 4 (2 5 7 8 9 11 12 13)) (7 5 (6 8 9 10 11 12 13)) (7 6 (5 7 9 12 14)) (7 8 (2 5 7 8 11 13)) (7 9 (2 5 6 8 9 11 12)) (7 10 (2 5 6 8 9 11 12 14)) (7 12 (5 6 8 9 10 11 13 14)) (7 13 (2 5 6 7 8 10 11 14)) (7 14 (5 6 7 10 12 13 14)) (8 1 (9 10 11 14)) (8 4 (5 7 9 11 12 13)) (8 7 (5 7 9 11 12 14)) (8 8 (5 7 11 13)) (8 9 (1 5 9 11 12)) (8 10 (1 5 9 11 12 14)) (8 12 (5 9 10 11 13 14)) (8 13 (1 5 7 10 11 14)) (8 14 (5 7 10 12 13 14)) (9 2 (3 7 8)) (9 3 (3 6 7 8 9 10)) (9 4 (3 4 7 8 9 11)) (9 5 (3 6 8 9 10 11)) (9 9 (3 4 6 8 9 11)) (9 10 (4 6 8 9 11)) (9 12 (3 4 6 8 9 10 11)) (9 13 (3 4 6 7 8 10 11)) (10 1 (8 9 11 14)) (10 4 (3 7 8 9 11 12 13)) (10 5 (3 6 8 9 11 12 13)) (10 6 (3 7 9 12 14)) (10 9 (3 6 8 9 11 12)) (10 10 (6 8 9 11 12 14)) (10 11 (6 7 11 12)) (10 13 (3 6 7 8 11 14)) (10 14 (3 6 7 12 13 14)) (11 1 (4 8 10 11)) (11 3 (3 6 7 8 10 13)) (11 4 (3 4 5 7 8 11 13)) (11 5 (1 3 6 8 10 11 13)) (11 6 (1 3 4 5 7)) (11 7 (5 6 7 11)) (11 8 (3 5 7 8 11 13)) (11 9 (1 3 4 5 6 8 11)) (11 10 (1 4 5 6 8 11)) (11 12 (3 4 5 6 8 10 11 13)) (12 2 (3 5 12)) (12 6 (3 5 12 14)) (12 7 (5 12 14)) (12 12 (3 5 14)) (13 4 (5 7 8 9 12)) (13 5 (6 8 9 10 12)) (13 7 (5 6 7 9 12)) (13 9 (5 6 8 9 12)) (13 10 (5 6 8 9 12)) (13 12 (5 6 8 9 10)) (13 13 (5 6 7 8 10)) (14 1 (4 8 9 11)) (14 2 (3 5 7 8 12)) (14 3 (3 6 7 8 9 12)) (14 4 (3 4 5 7 8 9 11 12)) (14 5 (1 3 6 8 9 11 12)) (14 8 (3 5 7 8 11)) (14 10 (1 4 5 6 8 9 11 12)) (14 11 (5 6 7 11 12)) (14 13 (1 3 4 5 6 7 8 11)) (14 14 (3 4 5 6 7 12))) 14)))
