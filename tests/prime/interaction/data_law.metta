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
