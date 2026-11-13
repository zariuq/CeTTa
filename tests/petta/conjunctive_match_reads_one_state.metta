; A conjunctive match answers from the space as it was when the match was
; called, also when the program adds or removes atoms between its answers.

; One application of a two-hop rule adds the same edges whatever order the
; facts were stored in.
!(bind! &forward (new-space))
!(add-atom &forward (edge a b))
!(add-atom &forward (edge b c))
!(add-atom &forward (edge c d))
!(collapse (match &forward (, (edge $x $y) (edge $y $z)) (add-atom &forward (edge $x $z))))
!(test (sort-atom (collapse (match &forward (edge $x $y) ($x $y))))
       (sort-atom ((a b) (b c) (c d) (a c) (b d))))

!(bind! &backward (new-space))
!(add-atom &backward (edge c d))
!(add-atom &backward (edge b c))
!(add-atom &backward (edge a b))
!(collapse (match &backward (, (edge $x $y) (edge $y $z)) (add-atom &backward (edge $x $z))))
!(test (sort-atom (collapse (match &backward (edge $x $y) ($x $y))))
       (sort-atom ((c d) (b c) (a b) (b d) (a c))))

; Atoms that are never in the space together do not answer a conjunction.
!(bind! &times (new-space))
!(add-atom &times (left a))
!(add-atom &times (left b))
!(add-atom &times (right a))
!(test (sort-atom (collapse (let $answer (match &times (, (left $x) (right $x)) $x)
                   (let $_gone (remove-atom &times (left b))
                     (let $_new (add-atom &times (right b)) $answer)))))
       (sort-atom (a)))

; An atom removed after the call still answers; one added after it does not,
; and equal atoms count once each.
!(bind! &removed (new-space))
!(add-atom &removed (left a))
!(add-atom &removed (left b))
!(add-atom &removed (right a))
!(add-atom &removed (right b))
!(test (sort-atom (collapse (let $x (match &removed (, (left $x) (right $x)) $x)
                   (let $_ (remove-atom &removed (right $any)) $x))))
       (sort-atom (a b)))
!(test (sort-atom (collapse (match &removed (right $x) $x)))
       (sort-atom ()))

; Removing the atom that was just matched does not answer it again.
!(bind! &consumed (new-space))
!(add-atom &consumed (left a))
!(add-atom &consumed (left b))
!(add-atom &consumed (right a))
!(add-atom &consumed (right b))
!(test (sort-atom (collapse (let $x (match &consumed (, (left $x) (right $x)) $x)
                   (let $_ (remove-atom &consumed (right $x)) $x))))
       (sort-atom (a b)))
!(test (sort-atom (collapse (let $x (match &consumed (, (left $x) (left $x)) $x)
                   (let $_ (remove-atom &consumed (left $x)) $x))))
       (sort-atom (a b)))

; Every occurrence read at entry answers, also after its one-copy removal.
!(bind! &copies (new-space))
!(add-atom &copies (f 1))
!(add-atom &copies (f 1))
!(test (size-atom (collapse (match &copies (, (f $x) (f $y))
                              (remove-atom &copies (f $y)))))
       4)
!(test (collapse (match &copies (f $x) $x)) ())

; A stored atom may hold a variable. It still answers after a removal with a
; narrower pattern has taken it.
!(bind! &open (new-space))
!(add-atom &open (left a))
!(add-atom &open (left b))
!(add-atom &open (right $any))
!(test (sort-atom (collapse (let $x (match &open (, (left $x) (right $x)) $x)
                   (let $_ (remove-atom &open (right a)) $x))))
       (sort-atom (a b)))
!(test (sort-atom (collapse (match &open (right $x) found)))
       (sort-atom ()))

!(bind! &flagged (new-space))
!(add-atom &flagged (left a))
!(add-atom &flagged (left b))
!(add-atom &flagged (flag $any))
!(test (sort-atom (collapse (let $x (match &flagged (, (left $x) (flag 3)) $x)
                   (let $_ (remove-atom &flagged (flag 2)) $x))))
       (sort-atom (a b)))

!(bind! &equal (new-space))
!(add-atom &equal (left a))
!(add-atom &equal (left a))
!(add-atom &equal (right a))
!(test (sort-atom (collapse (let $x (match &equal (, (left $x) (right $x)) $x)
                   (let $_ (add-atom &equal (right a)) $x))))
       (sort-atom (a a)))

; Three patterns, with a write that would satisfy the third.
!(bind! &three (new-space))
!(add-atom &three (p a))
!(add-atom &three (p b))
!(add-atom &three (q a))
!(add-atom &three (q b))
!(add-atom &three (r a))
!(test (sort-atom (collapse (let $x (match &three (, (p $x) (q $x) (r $x)) $x)
                   (let $_ (add-atom &three (r b)) $x))))
       (sort-atom (a)))

; The program still reads its own writes, and a match called later starts
; from the space as it is then.
!(bind! &own (new-space))
!(add-atom &own (left a))
!(add-atom &own (left b))
!(add-atom &own (right a))
!(add-atom &own (right b))
!(test (sort-atom (collapse (let $x (match &own (, (left $x) (right $x)) $x)
                   (let $_ (add-atom &own (seen $x))
                     (size-atom (collapse (match &own (seen $s) $s)))))))
       (sort-atom (1 2)))

!(bind! &later (new-space))
!(add-atom &later (left a))
!(add-atom &later (left b))
!(add-atom &later (right a))
!(test (sort-atom (collapse (let $x (match &later (, (left $x) (right $x)) $x)
                   (let $_ (add-atom &later (right b))
                     (sort-atom (collapse (match &later (, (left $y) (right $y)) $y)))))))
       (sort-atom ((a b))))

; Without writes between answers, the answers and their duplicates are unchanged.
!(bind! &plain (new-space))
!(add-atom &plain (p 1))
!(add-atom &plain (p 1))
!(add-atom &plain (p 2))
!(add-atom &plain (q 1 one))
!(add-atom &plain (q 2 two))
!(test (sort-atom (collapse (match &plain (, (p $x) (q $x $n)) ($x $n))))
       (sort-atom ((1 one) (1 one) (2 two))))
!(test (sort-atom (collapse (match &plain (, (p $x) (q $x $n) (p 2)) $n)))
       (sort-atom (one one two)))
!(test (sort-atom (collapse (match &plain (, (p 3) (q 3 $n)) $n)))
       (sort-atom ()))

; Nested matches remain separate reads: the inner one sees the space when it runs.
!(bind! &nested (new-space))
!(add-atom &nested (left a))
!(add-atom &nested (left b))
!(add-atom &nested (right a))
!(test (sort-atom (collapse (let $answer (match &nested (left $x) (match &nested (right $x) $x))
                   (let $_ (add-atom &nested (right b)) $answer))))
       (sort-atom (a b)))

; A list cell with an open tail removed during the conjunction, which no leg
; reads: the later leg still reads the space as it stood at the call.
!(bind! &cells (new-space))
!(add-atom &cells (left a))
!(add-atom &cells (left b))
!(add-atom &cells (right a))
!(add-atom &cells (right b))
!(let $l (cons-atom 1 $t) (add-atom &cells (junk $l)))
!(test (sort-atom (collapse (let $ans (match &cells (, (left $x) (right $y)) ($x $y))
                   (let $_ (remove-atom &cells (junk (1 2)))
                     (let $_2 (remove-atom &cells (right a))
                       (let $_3 (remove-atom &cells (right b)) $ans))))))
       (sort-atom ((a a) (a b) (b a) (b b))))

; Open list cells the later leg reads, both removed after the first answer.
!(bind! &opencells (new-space))
!(add-atom &opencells (left a))
!(add-atom &opencells (left b))
!(let $l (cons-atom a $t) (add-atom &opencells (right $l)))
!(let $l (cons-atom b $t) (add-atom &opencells (right $l)))
!(test (sort-atom (collapse (let $x (match &opencells (, (left $x) (right ($x 1 2))) $x)
                   (let $_ (remove-atom &opencells (right (a 1 2)))
                     (let $_2 (remove-atom &opencells (right (b 1 2))) $x)))))
       (sort-atom (a b)))
