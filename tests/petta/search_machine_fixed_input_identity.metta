!(import! &self (library lib_tabling))
!(tabled (fixed-flip $input))
(= (fixed-flip $input) left)
(= (fixed-flip $input) right)

; Equal call syntax still denotes two independent choices.
!(collapse (Pair (fixed-flip 7) (fixed-flip 7)))
; A caller can explicitly bind one choice and use it twice.
!(collapse (let $choice (fixed-flip 7) (Pair $choice $choice)))

!(import! &self (library lib_memo))
!(memoize fixed-repeat)
(= (fixed-repeat $input) $input)
(= (fixed-repeat $input) $input)
; Memo replay retains both answer occurrences even when payloads are equal.
!(collapse (fixed-repeat 7))
!(collapse (fixed-repeat 7))

!(tabled (fixed-set $input))
(= (fixed-set $input) $input)
(= (fixed-set $input) $input)
; Ordinary declared tables have their own set-valued answer contract.
!(collapse (fixed-set 7))
!(collapse (fixed-set 7))

!(memoize fixed-many)
(= (fixed-many $input) (superpose (0 1 2 3 4 5 6 7 8 9 10 11)))
; Larger collections exercise storage loans and retained-key compaction.
!(collapse (fixed-many (Payload 1 2 3 4 5 6 7 8)))
!(collapse (fixed-many (Payload 1 2 3 4 5 6 7 8)))
!(config-memoize (strategy wtinylfu) (unique-limit 1))
!(collapse (fixed-many (Payload 8 7 6 5 4 3 2 1)))
!(collapse (fixed-many (Payload 1 2 3 4 5 6 7 8)))
