; An observation of a first answer or of a finite prefix takes a `let`
; source's alternatives one at a time, and runs a body only when its outcome
; is taken.  How the source is spelled does not change how many bodies, and
; their effects, run: the literal choice (1 2 3) runs one body, as the
; equal-valued computed choice ((+ 0 1) 2 3) does.

!(once (let $x (superpose (1 2 3)) (println! (literal $x))))
!(once (let $x (superpose ((+ 0 1) 2 3)) (println! (computed $x))))
!(once (let $x (superpose (a b c)) (println! (symbols $x))))
!(once (let* (($x (superpose (1 2 3)))) (println! (let-star $x))))
!(once (let $z (superpose (1 2 3)) (let $x $z (println! (nested $x)))))
!(collapse (select 2 (let $x (superpose (1 2 3)) (println! (prefix $x)))))

; The same holds for a choice whose alternatives are a list's elements.
!(once (let $x (superpose [1 2 3]) (println! (list $x))))

; Negative: an observation of every outcome runs every body, once each.
!(collapse (let $x (superpose (1 2 3)) (println! (every $x))))
