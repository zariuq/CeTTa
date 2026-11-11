;; Heuristics as MeTTa data: (asp:decide S A V) chooses the session's next
;; decision; the search stays sound and complete whatever is decided.
;; (asp:nogoods S) shows the nogoods it has made.
!(import! &self asp)

;; one of a, b, c; the default search and a steered one find different models
(= (prog) ((choice a () ()) (choice b () ()) (choice c () ())
           (constraint (a b) ()) (constraint (a c) ()) (constraint (b c) ())
           (constraint () (a b c))))
!(asp:solve (prog))

;; decide each atom of ORDER true in turn, then run the search out
(= (steer $s $order)
   (if (== $order ())
       (let $r (asp:step $s) (if (== (car-atom $r) done) (index-atom $r 1) (steer $s ())))
       (let ($a $rest) (decons-atom $order)
         (let $d (asp:decide $s $a True)
           (if (== $d decided)
               (let $r (asp:step $s) (if (== (car-atom $r) done) (index-atom $r 1) (steer $s $rest)))
               (steer $s $rest))))))
!(let $s (asp:start (prog)) (steer $s (c)))
!(let $s (asp:start (prog)) (steer $s (b)))
;; a decision shows on the trail as a decision (reason -1) at its literal
!(let* (($s (asp:start (prog))) ($d (asp:decide $s c True)) ($r (asp:step $s)))
   (let (state $level $conflicts (trail $t) (true $ts) (false $fs)) (asp:state $s)
     ($d $level $ts $t)))
;; an atom with a value already is not decided again
!(let* (($s (asp:start ((rule a () ()) (choice b () ())))) ($r (asp:step $s)))
   (asp:decide $s a False))
;; steering into a dead end: the search backtracks and still answers rightly
!(let $s (asp:start ((choice a () ()) (choice b () ()) (constraint (a) ()) (constraint () (b)))) (steer $s (a)))
!(let $s (asp:start ((choice a () ()) (constraint (a) ()) (constraint () (a)))) (steer $s (a)))

;; nogoods learned: every assignment of five atoms ruled out but by search
(= (bits $n) (if (== $n 0) (()) (let $r (bits (- $n 1))
   (union-atom (collapse (let $x (superpose $r) (cons-atom 0 $x))) (collapse (let $x (superpose $r) (cons-atom 1 $x)))))))
(= (block $xs $atoms $p $n)
   (if (== $xs ()) (constraint $p $n)
     (let* ((($x $xr) (decons-atom $xs)) (($a $ar) (decons-atom $atoms)))
       (if (== $x 1) (block $xr $ar (cons-atom $a $p) $n) (block $xr $ar $p (cons-atom $a $n))))))
(= (hard) (let $atoms (x1 x2 x3 x4 x5)
   (union-atom (collapse (let $a (superpose $atoms) (choice $a () ())))
               (collapse (let $sp (bits 5) (let $xs (superpose $sp) (block $xs $atoms () ())))))))
(= (run-learning $s $n)
   (let (nogoods $gs) (asp:nogoods $s)
     (if (> (size-atom $gs) 0) (learned (> $n 0) (== (get-metatype (car-atom (car-atom $gs))) Grounded))
       (let $r (asp:step $s) (if (== (car-atom $r) done) (no-nogoods-before $r) (run-learning $s (+ $n 1)))))))
!(let $s (asp:start (hard)) (run-learning $s 0))
!(let $s (asp:start (hard)) (let $r (steer $s (x5 x4 x3)) (car-atom $r)))
!(car-atom (asp:solve (hard)))

;; refused
!(let $s (asp:start (prog)) (asp:decide $s d True))
!(let $s (asp:start (prog)) (asp:decide $s a maybe))
