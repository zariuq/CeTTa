(= (coin) (superpose (heads tails)))
; Which side select and once keep is open: either is a toss.
!(case (select (coin)) ((heads tossed) (tails tossed) ($other $other)))
!(case (once (coin)) ((heads tossed) (tails tossed) ($other $other)))
