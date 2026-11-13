; Positions and their looks in the compiled presentations: an expression is
; written as a call where its position runs and as data where it is held.
; The presentation gate prints this program in every presentation and checks
; the printings in (tests/petta/presentation_looks.*.expected).
!(import! &self (library lib_transform))
!(import! &self (library lib_flow))

; Atoms put in the program's space, and definitions whose heads are call
; patterns with data arguments.
(edge a b)
(edge b c)
(= (len ()) 0)
(= (len (cons $h $t)) (+ 1 (len $t)))
!(len (cons x (cons y ())))
!(collapse (match &self (edge $x $y) (pair $x $y)))

; A declared Atom position holds its argument; an undeclared one runs it.
(: show-held (-> Atom Atom))
(= (show-held $x) $x)
(= (twice $x) (* 2 $x))
!(show-held (twice 3))
!(no-such-function (twice 3))
; So a list written in an untyped position is a call of its head: steps
; runs its argument, and (build check deploy) answers built.
(= (build $x $y) built)
(= (steps $xs) $xs)
!(steps (build check deploy))
!(show-held (build check deploy))

; superpose holds its list and runs each item: the list is never a call.
!(collapse (superpose ((twice 1) (twice 2))))
; get-metatype runs its argument in this lane: (twice 3) is 6, Grounded.
!(get-metatype (twice 3))

; A fact and the statement that adds the same atom where it stands: the
; space is the same, but the statement answers true, which prints.
(color red)
!(add-atom &self (color green))
!(collapse (match &self (color $c) $c))

; A space holds what is added; patterns are data, match's template runs.
!(bind! &s (new-space))
!(add-atom &s (job j1 2))
!(add-atom &s (job j2 0))
!(add-atom &s (busy j2))
!(collapse (match &s (job $j $n) (+ $n 1)))

; transform: reads, absence, the explicit read, a take, and answer sets.
!(collapse (transform &s (, (job $j $n) (absent (busy $j))) (free $j)))
!(transform &s (, (take (job $j 0)) (read (busy $j))) (done $j) &s)
!(collapse (transform &s (once (job $j $n)) $j))
!(collapse (transform &s (collect (job $j $n) $all) (size-atom $all)))

; Flows: a loop over a list's items (each item runs, as superpose's), a loop
; over a computation's answers, a sequence, a loop over matches.
!(flow:for-each (superpose (a b)) (|-> ($x) (println! (item $x))))
!(flow:for-each (superpose ((twice 1) b)) (|-> ($x) (println! (item $x))))
!(flow:for-each (twice 2) (|-> ($x) (println! (answer $x))))
!(flow:then (println! first) second)
!(flow:for-match &s (job $j $n) (println! (has $j)))
