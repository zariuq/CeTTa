; In the extended profile `select k` answers some k of its body's answers,
; as `once` answers some one: depth-first search and iterative deepening
; share the work, so a body whose depth-first order never returns still
; has its answers, each derivation counted once.  Which k it takes is open:
; a select is read sorted where its body has no more answers than asked,
; and otherwise by how many it took and how many of them are distinct, or
; which of them its body cannot give.
(= (loop) (loop))
(= (distinct-count $taken) (selected (size-atom $taken) (size-atom (unique-atom $taken))))
(= (taken-within $taken $all) (selected (size-atom $taken) (subtraction-atom $taken $all)))

; The first equation never returns; the others' answers keep their number.
(= (e) (loop))
(= (e) x)
(= (e) x)
!(sort-atom (select 2 (e)))

; `y` is reached at two depths: once directly, once through `f1`.
(= (f) (loop))
(= (f) y)
(= (f) (f1))
(= (f1) y)
(= (f1) z)
!(sort-atom (select 3 (f)))

; An infinite relation whose recursive equation comes first.
(= (nat $n) (nat (S $n)))
(= (nat $n) $n)
!(distinct-count (select 5 (nat Z)))

; A cheap body gives some of its answers; a body with fewer answers than
; asked answers all of them.
(= (d) a)
(= (d) a)
(= (d) b)
!(taken-within (select 2 (d)) (a a b))
!(sort-atom (select 7 (d)))
