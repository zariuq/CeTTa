; One declarative B2/S2 HexLife generation in ordinary Prime MeTTa.
; The six neighbor schemas and three initial live cells are the same data as
; hexlife_structural.mm2.  Generic match and fold-by-key compute the next set.

(neighbors ($q $r (S $s)) ($q (S $r) $s))
(neighbors ($q $r (S $s)) ((S $q) $r $s))
(neighbors ($q (S $r) $s) ((S $q) $r $s))
(neighbors ($q (S $r) $s) ($q $r (S $s)))
(neighbors ((S $q) $r $s) ($q (S $r) $s))
(neighbors ((S $q) $r $s) ($q $r (S $s)))

(alive ((S (S Z)) (S Z) (S Z)))
(alive ((S Z) (S (S Z)) (S Z)))
(alive ((S Z) (S (S (S Z))) Z))

(= (hexlife-neighbor-hits)
   (match &self (alive $source)
     (match &self (neighbors $source $target)
       (neighbor-hit $target 1))))

(= (hexlife-next)
   (let ($cell $count)
        (fold-by-key
          (hexlife-neighbor-hits)
          0
          $acc
          $item
          (let (neighbor-hit $key $one) $item $key)
          (eval (+ $acc 1)))
     (if (== $count 2)
         (alive $cell)
         (superpose ()))))

!(collapse (hexlife-next))
