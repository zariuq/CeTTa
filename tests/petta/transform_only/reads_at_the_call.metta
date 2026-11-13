; A match answers from the store as it is when it is called: the code that
; consumes its answers removes a row not answered yet, or adds one.
!(bind! &t (new-space))
!(add-atom &t (p 1))
!(add-atom &t (p 2))
(= (each) (match &t (p $x) $x))
(= (drop-two $x) (let $_gone (remove-atom &t (p 2)) $x))
!(test (collapse (drop-two (each))) (1 2))

!(bind! &u (new-space))
!(add-atom &u (q 1))
(= (every) (match &u (q $x) $x))
(= (add-two $x) (let $_made (add-atom &u (q 2)) $x))
!(test (collapse (add-two (every))) (1))

; CeTTa reads a conjunction from one snapshot. A row added by the consumer
; of the first answer belongs to a later read, not the current conjunction.
!(bind! &s (new-space))
!(add-atom &s (left a))
!(add-atom &s (left b))
!(add-atom &s (right a))
(= (joined) (match &s (, (left $x) (right $x)) $x))
(= (on-answer $x) (let $_made (add-atom &s (right b)) $x))
!(test (collapse (on-answer (joined))) (a))

; The program's own space is read the same way.
(fact 1)
(fact 2)
(= (facts) (match &self (fact $x) $x))
(= (drop-fact $x) (let $_gone (remove-atom &self (fact 2)) $x))
!(test (collapse (drop-fact (facts))) (1 2))
