; Rholang's receipts are the same firings, spelled as receipts.  Each pair
; below leaves the same store.
!(import! &self interaction)
!(bind! &w (new-space))
!(bind! &r (new-space))

; a linear receive: await of a take
!(bind! &c1 (new-space))
!(bind! &c2 (new-space))
!(await (transform &c1 (take (msg $m)) (add-atom &w (got $m))))
!(for ((msg $m) <- &c2) (add-atom &r (got $m)))
!(transform &c1 () (, (add-atom &c1 (msg 1)) (add-atom &c1 (msg 2))))
!(transform &c2 () (, (add-atom &c2 (msg 1)) (add-atom &c2 (msg 2))))
!(collapse (match &w (got $m) $m))
!(collapse (match &r (got $m) $m))
!(collapse (match &c1 (msg $m) $m))
!(collapse (match &c2 (msg $m) $m))

; a persistent receive: whenever of a take; contract is the same
!(bind! &d1 (new-space))
!(bind! &d2 (new-space))
!(bind! &d3 (new-space))
!(whenever (transform &d1 (take (ping $k)) (add-atom &w (pong $k))))
!(for ((ping $k) <= &d2) (add-atom &r (pong $k)))
!(contract &d3 (ping $k) (add-atom &r (pong3 $k)))
!(transform &d1 () (, (add-atom &d1 (ping 1)) (add-atom &d1 (ping 2))))
!(transform &d2 () (, (add-atom &d2 (ping 1)) (add-atom &d2 (ping 2))))
!(transform &d3 () (, (add-atom &d3 (ping 1)) (add-atom &d3 (ping 2))))
!(sort-atom (collapse (match &w (pong $k) $k)))
!(sort-atom (collapse (match &r (pong $k) $k)))
!(sort-atom (collapse (match &r (pong3 $k) $k)))

; a peek: await of a read; the message stays
!(bind! &e1 (new-space))
!(bind! &e2 (new-space))
!(await (transform &e1 (read (flag $f)) (add-atom &w (peeked $f))))
!(for ((flag $f) <<- &e2) (add-atom &r (peeked $f)))
!(transform &e1 () (add-atom &e1 (flag up)))
!(transform &e2 () (add-atom &e2 (flag up)))
!(collapse (match &w (peeked $f) $f))
!(collapse (match &r (peeked $f) $f))
!(collapse (match &e1 (flag $f) $f))
!(collapse (match &e2 (flag $f) $f))

; a join: two takes in one commit
!(bind! &a1 (new-space))
!(bind! &b1 (new-space))
!(bind! &a2 (new-space))
!(bind! &b2 (new-space))
!(await (transform &a1 (, (take (x $u)) (take &b1 (y $v))) (add-atom &w (pair $u $v))))
!(for ((x $u) <- &a2 & (y $v) <- &b2) (add-atom &r (pair $u $v)))
!(transform &a1 () (add-atom &a1 (x 1)))
!(transform &a2 () (add-atom &a2 (x 1)))
!(collapse (match &w (pair $u $v) ($u $v)))
!(transform &b1 () (add-atom &b1 (y 2)))
!(transform &b2 () (add-atom &b2 (y 2)))
!(collapse (match &w (pair $u $v) ($u $v)))
!(collapse (match &r (pair $u $v) ($u $v)))

; one arrow kind per receipt, as in Rholang
!(for ((x $u) <- &a2 & (y $v) <= &b2) (add-atom &r mixed))
