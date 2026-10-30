; The data law: reading or taking stored code never runs it; eval does.
; Sources and templates are syntax: no source word is evaluated.
!(import! &self interaction)
!(bind! &code (new-space))
!(bind! &out (new-space))
!(add-atom &code (job (+ 1 2)))
!(add-atom &code (job (println! hijacked)))

; read: the value reaches the continuation as data
!(sort-atom (collapse (transform &code (every (read (job $c))) (as-data $c))))
; eval is the one step that runs it
!(transform &code (read (job (+ $a $b))) (eval (+ $a $b)))
; taking stored code and storing it elsewhere moves data; nothing ran
!(sort-atom (collapse (transform &code (every (take (job $c))) (add-atom &out (moved $c)))))
!(sort-atom (collapse (get-atoms &out)))

; unique is a source word here, not the library's dedupe
!(add-atom &code (lock a))
!(add-atom &code (lock b))
!(transform &code (unique (lock $x)) (only $x))
!(unique (superpose (1 1 2)))

; An ordinary call consumes a stored value without turning its syntax back
; into source. Structural patterns still inspect its fields.
!(bind! &data-law-effects (new-space))
(= (data-law:identity $x) $x)
(= (data-law:shape $x) (get-metatype $x))
(= (data-law:record (Envelope $x)) (Some $x))
(= (data-law:effect) (let $_ (add-atom &data-law-effects ran) 42))
(: data-law:syntax (-> Atom Atom))
(= (data-law:syntax $x) (data-law:shape $x))
(: data-law:value (-> Proc Proc))
(= (data-law:value $x) $x)
!(add-atom &out (source (data-law:effect)))
!(assertEqual (transform &out (read (source $x))
   (data-law:shape (data-law:identity $x))) Expression)
!(assertEqual (transform &out
   (, (read (source $x)) (let $y (data-law:identity $x)))
   (data-law:shape $y)) Expression)
!(add-atom &out (record (Envelope (data-law:effect))))
!(assertEqual (transform &out
   (, (read (record $x)) (let (Some $y) (data-law:record $x)))
   (data-law:shape $y)) Expression)
!(assertEqual (data-law:syntax (data-law:effect)) Expression)
!(assertEqual (collapse (get-atoms &data-law-effects)) ())

; Nondeterministic syntax stays the same syntax on both sides; a data read
; must not sample it twice and invent an inequality.
!(add-atom &out (choice (superpose (p q))))
(= (data-law:different $a $b) (not (== $a $b)))
!(assertEqual (collapse (transform &out
   (, (read (choice $x)) (let True (data-law:different $x $x)))
   unsound)) ())
!(assertEqual (transform &out
   (, (read (choice $x)) (let True (== $x $x))) reflexive) reflexive)

; Open stored syntax is data too, even after a structural matcher names
; its parts. A caller's Atom parameter remains a computed-value parameter.
!(add-atom &out (open (Envelope (println! ($free $free)))))
!(assertEqual (transform &out
   (, (read (open $x)) (let (Some $y) (data-law:record $x)))
   (data-law:shape $y)) Expression)
!(assertEqual (transform &out
   (, (read (source $x)) (let 42 (eval $x))) explicit) explicit)
!(assertEqual (collapse (get-atoms &data-law-effects)) (ran))
!(assertEqual (data-law:value (data-law:effect)) 42)
!(assertEqual (collapse (get-atoms &data-law-effects)) (ran ran))
