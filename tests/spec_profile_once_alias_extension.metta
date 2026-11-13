(= (coin) (superpose (heads tails)))
; Which side once keeps is open: either is a toss.
!(case (once (coin)) ((heads tossed) (tails tossed) ($other $other)))
