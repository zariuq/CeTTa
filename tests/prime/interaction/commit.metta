; One interaction is one commit: what it takes leaves, what it keeps stays,
; what it makes arrives, all at once.  Answers are compared as bags.
!(import! &self interaction)
!(bind! &s (new-space))
!(bind! &out (new-space))

; take, keep and make
!(add-atom &s (job a))
!(transform &s (take (job $x)) (took $x))
!(transform &s (take (job $x)) (took $x))
!(add-atom &s (rate 3))
!(add-atom &s (order 1))
!(transform &s (, (rate $r) (take (order $n))) (priced $n $r))
!(sort-atom (collapse (get-atoms &s)))

; identification: a row never keeps and takes one occurrence
!(add-atom &s Token)
!(transform &s (, (read Token) (take Token)) one-token-committed)
!(add-atom &s Token)
!(transform &s (, (read Token) (take Token)) two-tokens-committed)
!(collapse (match &s Token Token))
!(transform &s (, (take Token) (take Token)) double-take-committed)

; every: one row per occurrence, all in one commit
!(add-atom &s (job b))
!(add-atom &s (job b))
!(add-atom &s (job c))
!(sort-atom (collapse (transform &s (every (read (job $x))) (seen $x))))
!(sort-atom (collapse (transform &s (every (take (job $x))) (took $x))))
!(collapse (match &s (job $x) $x))
; an every with no rows still commits its kernel
!(transform &s (, (take (rate $r)) (every (take (job $x)))) (rate-taken $r))
; every refuses rows that take a common occurrence
!(add-atom &s (item 1))
!(add-atom &s (item 2))
!(add-atom &s gate)
!(transform &s (every (, (read (item $x)) (take gate))) both-took-the-gate)
!(collapse (match &s gate gate))
; and rows where one keeps what another takes
!(add-atom &s (p 1))
!(add-atom &s (p 2))
!(transform &s (every (, (read (p $x)) (take (p $y)))) keep-and-take-across-rows)
!(sort-atom (collapse (match &s (p $x) $x)))

; predicate queries: unique, all, absent
!(add-atom &s (lock a))
!(add-atom &s (lock b))
!(transform &s (unique (lock $x)) (only $x))
!(transform &s (unique take (lock a)) took-lock-a)
!(transform &s (unique (lock $x)) (only $x))
!(transform &s (, (all (p $x) $ps) (let $n (size-atom $ps))) (count $n))
!(transform &s (absent (job $x)) no-jobs)
!(transform &s (absent (p $x)) no-p)
!(transform &s (absent (, (p $x) (lock $x))) no-p-with-its-lock)
!(transform &s (all take (p $x) $ps) (cleared (size-atom $ps)))
!(collapse (match &s (p $x) $x))

; let: code runs once its variables are bound; no value, no row
!(add-atom &s (balance alice 50))
!(transform &s (, (take (balance alice $a)) (let $b (- $a 20)) (let True (>= $b 0)))
   (, (add-atom &s (balance alice $b)) (add-atom &out (paid alice 20)) (left $b)))
!(transform &s (, (take (balance alice $a)) (let $b (- $a 100)) (let True (>= $b 0)))
   (, (add-atom &s (balance alice $b)) (add-atom &out (paid alice 100)) (left $b)))
!(collapse (match &s (balance alice $v) $v))
!(collapse (get-atoms &out))

; with a destination, every instance is made there and the answer is how many
!(bind! &dst (new-space))
!(add-atom &s (word x))
!(add-atom &s (word y))
!(transform &s (every (read (word $w))) (copied $w) &dst)
!(sort-atom (collapse (get-atoms &dst)))

; every rows range over the snapshot; each is linear on its own, and the
; whole commit must be: a kernel that takes what a row keeps, or keeps what
; rows take, does not fire
!(bind! &e (new-space))
!(add-atom &e (a))
!(add-atom &e (a))
!(transform &e (, (take (a)) (every (read (a)))) kernel-took-a-kept-row)
!(transform &e (, (read (a)) (every (take (a)))) kernel-kept-a-taken-row)
!(collapse (match &e (a) found))
; with the kernel's occurrence apart from the rows' region, it fires
!(add-atom &e (b))
!(sort-atom (collapse (transform &e (, (take (b)) (every (read (a)))) row)))
!(collapse (match &e $x $x))
