; HE completes every evaluation before its consumer reads it: the effects of
; a producer's alternatives all happen first, an error beside a success is
; dropped, an argument's error replaces the evaluation that reads it, and a
; match's bindings reach the evaluation that reads them.

; A one-equation callee whose body superposes effects: its caller reads
; after both.
(= (put-both $xs) (let $x (superpose $xs) (add-atom &self (pb $x))))
(= (read-both) (let $u (put-both (1 2)) (collapse (match &self (pb $y) $y))))
!(read-both)

; Within one body the superpose's alternatives run one after the other.
(= (each-reads $xs)
   (let $x (superpose $xs)
        (let $u (add-atom &self (er $x)) (collapse (match &self (er $y) $y)))))
!(each-reads (1 2))

; A case completes as one producer.
(= (case-both)
   (let $u (case (superpose (1 2)) (($x (add-atom &self (cb $x)))))
        (collapse (match &self (cb $y) $y))))
!(case-both)

; A let whose value is a let over a branching producer completes first.
(= (let-both)
   (let $u (let $y (superpose (1 2)) (add-atom &self (lb $y)))
        (collapse (match &self (lb $w) $w))))
!(let-both)

; An argument that branches with effects completes before the call.
(= (read-arg $u) (collapse (match &self (ab $w) $w)))
(= (arg-both) (read-arg (let $y (superpose (1 2)) (add-atom &self (ab $y)))))
!(arg-both)

; A collapse drops an error beside a success.
(= (keep-success $v) (collapse (superpose ($v (+ $v "S")))))
!(keep-success 1)

; A collapse of errors keeps them; read as an argument it gives the first.
(= (first-error $v)
   (let $r (collapse (superpose ((+ $v "S") (+ $v "T")))) (Box $r)))
!(collapse (first-error 1))

; An argument's error replaces the evaluation that reads it.
(= (pair-fault) (let $y (+ 1 "S") (pair $y 2)))
!(collapse (pair-fault))

; A match's bindings reach the let body after it, for a call that streams
; and for one that does not.
(p 1)
(p 2)
(= (bound) (let $r (match &self (p $x) ok) (pair $r $x)))
!(bound)
(= (bound-a a) (let $r (match &self (p $x) ok) (pair $r $x)))
!(bound-a a)

; A template passed by destination still answers its evaluated instance.
(op (+ 1 2))
(op (+ 2 2))
(= (ops) (match &self (op $t) $t))
!(ops)
(= (wrapped) (collapse (match &self (op $t) (wrap $t))))
!(wrapped)

; One variable of a producer stays one across its collected alternatives:
; distinct variables could take 11 and 22, one shared variable cannot.
(= (shared-fresh a)
   (collapse (let $v (case (superpose (a b)) ((a 1) (b 2))) (Pair $v $fresh))))
!(let $items (shared-fresh a)
   (case $items
     ((((Pair 1 11) (Pair 2 22)) distinct)
      (((Pair 2 22) (Pair 1 11)) distinct)
      ($otherwise shared))))

; A match's bindings reach the let body when the let's pattern is one of
; the match's variables, and when a row's value still reduces.
(q 1 2)
(q 3 4)
(= (subset a) (let $x (match &self (q $x $y) $x) (Pair $x $y)))
!(subset a)
(rr (+ 1 1) A)
(= (reducible a) (let $r (match &self (rr $x $y) $x) (Triple $r $x $y)))
!(let $actual (reducible a) (== $actual (Triple 2 2 A)))

; A structure's fields fault in order: the left field's error answers.
(= (cmp $x) (if (> $x 1) big small))
(= (fields $x) (Box (cmp $x) (+ $x 1)))
!(fields 5)
!(fields "S")
