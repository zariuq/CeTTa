; In the extended profile `select k` answers some k of its body's answers,
; as `once` answers some one: depth-first search and iterative deepening
; share the work, so a body whose depth-first order never returns still
; has its answers, each derivation counted once.
(= (loop) (loop))

; The first equation never returns; the others' answers keep their number.
(= (e) (loop))
(= (e) x)
(= (e) x)
!(select 2 (e))

; `y` is reached at two depths: once directly, once through `f1`.
(= (f) (loop))
(= (f) y)
(= (f) (f1))
(= (f1) y)
(= (f1) z)
!(select 3 (f))

; An infinite relation whose recursive equation comes first.
(= (nat $n) (nat (S $n)))
(= (nat $n) $n)
!(select 5 (nat Z))

; A cheap depth-first search answers in its own order; a body with fewer
; answers than asked answers all of them.
(= (d) a)
(= (d) a)
(= (d) b)
!(select 2 (d))
!(select 7 (d))
