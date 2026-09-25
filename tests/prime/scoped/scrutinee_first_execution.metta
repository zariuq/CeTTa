; Executing a definition admitted through its scrutinee-first form.
;
; In the kernel, `add-onto` unfolds to its scrutinee-first form at any
; arguments, and that form computes only when its first argument is a
; constructor form: the only argument either demands is the authored
; scrutinee. An accumulator the kernel knows nothing about is carried along
; and returned; a call whose scrutinee is unknown stays a call.
!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0))))
!(set:inductive &thy num (u 0) (: zero num) (: succ (-> num num)))
!(pf:theory &thy ((: q num)))
!(set:define &thy add-onto (-> num num num)
   (= (add-onto $acc zero) $acc)
   (= (add-onto $acc (succ $n)) (add-onto (succ $acc) $n)))
!(set:eq &thy (add-onto q zero) q)
!(set:eq &thy (add-onto q (succ (succ zero))) (succ (succ q)))
!(set:eq &thy (add-onto zero q) (add-onto zero q))
!(set:eq &thy (add-onto zero q) zero)

; The same rewriting on ordinary equations, run by the call-by-need
; evaluator: the authored equations and their scrutinee-first form give the
; same results, demand the same arguments, keep quoted arguments as syntax and
; share an argument used twice.
(= (onto-a $acc Z) $acc)
(= (onto-a $acc (S $n)) (onto-a (S $acc) $n))
(= (onto-f Z $acc) $acc)
(= (onto-f (S $n) $acc) (onto-f $n (S $acc)))
(= (onto-t $acc $n) (onto-f $n $acc))
!(onto-a (S Z) (S (S Z)))
!(onto-t (S Z) (S (S Z)))

; The accumulator is never inspected: evaluating it would fault.
(= (drop-a $acc Z) Z)
(= (drop-a $acc (S $n)) (drop-a (S $acc) $n))
(= (drop-f Z $acc) Z)
(= (drop-f (S $n) $acc) (drop-f $n (S $acc)))
(= (drop-t $acc $n) (drop-f $n $acc))
!(drop-a (== 1 "unused-fault") (S (S Z)))
!(drop-t (== 1 "unused-fault") (S (S Z)))

; A quoted accumulator is returned as syntax.
!(onto-a (quote (+ 1 2)) Z)
!(onto-t (quote (+ 1 2)) Z)

; An argument used twice is one choice.
(= (twice-a $acc Z) (pair $acc $acc))
(= (twice-a $acc (S $n)) (twice-a $acc $n))
(= (twice-f Z $acc) (pair $acc $acc))
(= (twice-f (S $n) $acc) (twice-f $n $acc))
(= (twice-t $acc $n) (twice-f $n $acc))
!(twice-a (superpose (1 2)) (S Z))
!(twice-t (superpose (1 2)) (S Z))

; A call whose scrutinee is unknown stays a call; the rewritten form shows
; its own head.
!(onto-a (S Z) mystery)
!(onto-t (S Z) mystery)
