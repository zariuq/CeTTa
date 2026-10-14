; CeTTa PeTTa's explicit cons-spine boundary, also used by native grammar trees.
!(GuestWire flat
   (case (quote (a b c))
     (((cons $head $tail) (== $tail (quote (b c)))) ($bad False))))
!(GuestWire spine
   (case (quote (cons a nil))
     (((cons $head $tail) (if (== $head a) (== $tail nil) False)) ($bad False))))
!(GuestWire improper (== (cons a nil) (quote (cons a nil))))
!(GuestWire nested
   (case (cons a (cons b nil))
     (((cons $first (cons $second $rest))
       (if (== $first a) (if (== $second b) (== $rest nil) False) False))
      ($bad False))))
!(GuestWire once (== (collapse (once (superpose (first second)))) (quote (first))))
