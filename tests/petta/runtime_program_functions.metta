; A program may define functions under a special form's name.  Where the form
; is written it keeps its meaning.  Reached at run time, the name is the
; program's function and is applied like any other: its equations run at
; their own arity, and with fewer arguments it waits for the rest.  Its
; equations never run the form, and the form never runs in their place.

(= (let $a $b $c) (mine $a $b $c))
(= (let $a $b $c) (also $a $b $c))
(= (let $a $b $c $d) (user $a $b $c $d))
(= (if $a $b $c $d) (four $a $b $c $d))
(= (quote $a $b) (two $a $b))
(= (case $x) (wrapped $x))
(= (chain ok $b $c) (only-ok $b $c))

; Written, each form keeps its meaning.
!(let $x 1 (+ $x 1))
!(if True 1 2)

; Reached at run time, the program's function runs, at each of its arities.
!(let $f let ($f a b c))
!(let $f let ($f a b c d))
!(reduce (let a b c))
!(let $f if ($f a b c d))
!(let $f quote ($f a b))
!(map-atom (1 2) case)

; With fewer arguments than its equations take, it waits for the rest,
; after its arguments are evaluated.
!(let $f let ($f a b))
!(let $f if ($f True 1 2))
!(let $f quote ($f (+ 1 2)))
!(let $f let (let $p ($f a b c) ($p d)))

; A call whose equations all fail fails; it does not become data.
!(let $f chain ($f ok 1 2))
!(let $f chain ($f z 1 2))
!(collapse (let $f chain ($f z 1 2)))
