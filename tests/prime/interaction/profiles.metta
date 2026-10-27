; Regions carry profiles, kept by the region authority: the facets the
; holder has and the facets the holder may hand out.  Handles are opaque
; and carry their facets; a program is checked against them when formed.
!(import! &self interaction)
!(bind! &out (new-space))

; the catalogue
!(let $r (new-region space) (rights-of $r))
!(let $r (new-region channel) (rights-of $r))
!(let $r (new-region mailbox) (rights-of $r))
!(let $r (new-region log) (rights-of $r))
!(let $r (new-region board) (rights-of $r))
!(new-region bucket)

; strategies the profile licenses
!(let $r (new-region space) (strategy-of $r))
!(let $r (new-region channel) (strategy-of $r))
!(let $r (new-region log) (strategy-of $r))

; facets attenuate: within the holder's and within what the profile hands out
!(let $m (new-region mailbox) (rights-of (facet $m (send receive peek observe))))
!(let $b (new-region board) (rights-of (facet $b (peek send))))
!(let $c (new-region channel) (rights-of (facet (facet $c (send peek)) (receive peek))))
!(let $c (new-region channel) (is-handle (facet $c (send))))
!(is-handle &out)

; a mailbox: senders are blind; the holder receives
!(bind! &mbx (new-space))
!(let $m (new-region mailbox)
   (let $_ (add-atom &mbx (mailbox $m))
     (transform $m () (add-atom $m (letter 1)))))
!(let $m (transform &mbx (read (mailbox $h)) $h)
   (transform (facet $m (send)) (take (letter $n)) (stolen $n)))
!(let $m (transform &mbx (read (mailbox $h)) $h)
   (let $w (facet $m (send))
     (transform $w () (add-atom $w (letter 2)))))
!(let $m (transform &mbx (read (mailbox $h)) $h)
   (sort-atom (collapse (transform $m (every (take (letter $n))) (read-by-holder $n)))))

; a channel has no predicate queries, for anyone
!(let $c (new-region channel) (transform $c (absent (msg $x)) empty))
!(let $c (new-region channel) (transform $c (all (msg $x) $xs) (all $xs)))
; a log: nothing leaves
!(let $l (new-region log)
   (let $_ (transform $l () (add-atom $l (fact a)))
     (transform $l (take (fact $x)) (removed $x))))
!(let $l (new-region log)
   (let $_ (transform $l () (add-atom $l (fact a)))
     (transform $l (all (fact $x) $xs) (facts $xs))))
; a board: readers peek, only the holder writes
!(let $b (new-region board)
   (let $_ (transform $b () (add-atom $b (notice 1)))
     (transform (facet $b (peek)) (read (notice $n)) (saw $n))))
!(let* (($b (new-region board)) ($r (facet $b (peek))))
   (transform $r () (add-atom $r (notice 2))))
; a location is a name: code written there is data and names nothing
!(let $b (new-region board)
   (transform $b () (add-atom (facet $b (peek)) (notice 3))))

; a location written as a variable no earlier source binds reads every
; region: not formed, whatever the rights
!(transform &out (take $any (msg $x)) (got $x))
!(transform &out (, (read (svc $c)) (take $d (msg $x))) (got $x))
; a location bound by an earlier source is formed; its rights are checked
; when the walk reaches it
!(bind! &registry (new-space))
!(let $c (new-region channel)
   (let* (($_ (transform &registry () (add-atom &registry (svc full $c))))
          ($w (facet $c (send)))
          ($_2 (transform &registry () (add-atom &registry (svc send-only $w))))
          ($_3 (transform $c () (add-atom $c (msg 7)))))
     (transform &registry (, (read (svc full $ch)) (take $ch (msg $x))) (via-registry $x))))
!(transform &registry (, (read (svc send-only $ch)) (take $ch (msg $x))) (via-registry $x))
