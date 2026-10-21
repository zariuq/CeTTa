; A ready value is data when sort-atom or unique-atom reads it: its parts do
; not run (the field rows of elaborate_need_fixtures, where a field written
; as data, (+ 1 2), comes back as 3 once the answers are sorted).  The first
; and last rows are controls.
!(let $plain (car-atom (cons-atom (cons-atom (cons-atom Field ((cons-atom + (1 2)))) ()) ())) $plain)
!(let $plain (car-atom (cons-atom (cons-atom (cons-atom Field ((cons-atom + (1 2)))) ()) ())) (sort-atom $plain))
!(let $plain (car-atom (cons-atom (cons-atom (cons-atom + (1 2)) ()) ())) (sort-atom $plain))
!(let $plain (car-atom (cons-atom (cons-atom (cons-atom + (1 2)) ()) ())) (unique-atom $plain))
!(let $plain (car-atom ((b a))) (sort-atom $plain))
