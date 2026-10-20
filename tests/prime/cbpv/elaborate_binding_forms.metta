; Each binding form of the binding-site table, elaborated through its row
; and run by lib/prime/cbpv.metta, against the draft running the same
; program here (the native side names its functions with n-).  A row reads
; (Row label agrees elaborated observed), both bags sorted.
!(import! &self ../../../lib/prime/cbpv.metta)

(= (row $label (quote $e) $observed)
   (let $raw (cbpv:elaborated-bag (quote $e))
     (let $mine (sort-atom $raw)
       (let $theirs (sort-atom $observed)
         (Row $label (== $mine $theirs) $mine $theirs)))))

(= (n-pick) heads)
(= (n-pick) tails)
(cbpv:def coin () heads)
(cbpv:def coin () tails)

; case and switch: the source by value, the first clause that matches; no
; clause, no world.
!(row case-first (quote (s:case (coin) ((heads h) ((V other) (other (V other))))))
     (collapse (case (n-pick) ((heads h) ($other (other $other))))))
!(row case-none (quote (s:case (coin) ((ace a))))
     (collapse (case (n-pick) ((ace a)))))
!(row case-structure (quote (s:case (Pair 1 (coin)) (((Pair (V a) heads) (got (V a))) ((Pair (V a) (V b)) (else (V b))))))
     (collapse (case (Pair 1 (n-pick)) (((Pair $a heads) (got $a)) ((Pair $a $b) (else $b))))))
!(row switch (quote (s:switch (coin) ((heads h) (tails t))))
     (collapse (switch (n-pick) ((heads h) (tails t)))))

; chain and a let pattern: the source by value, then the pattern.
!(row chain (quote (s:chain (coin) (V c) (Seen (V c))))
     (collapse (chain (n-pick) $c (Seen $c))))
!(row let-pattern (quote (s:let (Pair (V a) (V b)) (Pair (coin) 2) (Both (V a) (V b))))
     (collapse (let (Pair $a $b) (Pair (n-pick) 2) (Both $a $b))))
!(row let-pattern-fails (quote (s:let (Pair (V a) 3) (Pair 1 2) (V a)))
     (collapse (let (Pair $a 3) (Pair 1 2) $a)))

; unify: neither side computed; the pattern may be on either side.
!(row unify-left (quote (s:unify (g (V x)) (g 5) (V x) no))
     (collapse (unify (g $x) (g 5) $x no)))
!(row unify-right (quote (s:unify (g 5) (g (V x)) (V x) no))
     (collapse (unify (g 5) (g $x) $x no)))
!(row unify-else (quote (s:unify (g 5) (h (V x)) (V x) no))
     (collapse (unify (g 5) (h $x) $x no)))

; map-atom and foldl-atom: the body once per element.
!(row map-template (quote (s:map-atom (1 2) (V x) (Pair (V x) (coin))))
     (collapse (map-atom (1 2) $x (Pair $x (n-pick)))))
!(row fold (quote (s:foldl-atom (1 2 3) 0 (V acc) (V x) (s:+ (V acc) (V x))))
     (collapse (foldl-atom (1 2 3) 0 $acc $x (+ $acc $x))))
