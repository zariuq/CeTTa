; Runtime half of a dependently typed vector example.  The graduality gate
; erases these declarations and requires the same computations and answers.

(: prime-grad:Nat Type)
(: prime-grad:Z prime-grad:Nat)
(: prime-grad:S (-> prime-grad:Nat prime-grad:Nat))
(: prime-grad:Vec (-> $t prime-grad:Nat Type))
(: prime-grad:Nil (prime-grad:Vec $t prime-grad:Z))
(: prime-grad:Cons (-> $t (prime-grad:Vec $t $n) (prime-grad:Vec $t (prime-grad:S $n))))
(: prime-grad:drop (-> (prime-grad:Vec $t (prime-grad:S $n)) (prime-grad:Vec $t $n)))

(= (prime-grad:drop (prime-grad:Cons $x $xs)) $xs)

!(prime-grad:drop
   (prime-grad:Cons 2
     (prime-grad:Cons 1 prime-grad:Nil)))
!(prime-grad:drop
   (prime-grad:drop
     (prime-grad:Cons 3
       (prime-grad:Cons 2
         (prime-grad:Cons 1 prime-grad:Nil)))))
