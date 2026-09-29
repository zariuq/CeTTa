; A conjunctive match whose answers are one bag in any order of its legs
; runs next, in each state, the leg with the fewest candidates under the
; bindings so far.  Where an effect could reach a later leg's snapshot, the
; legs keep their written order.  The answers are SWI-PeTTa's; each bag is
; sorted or counted, as search order is not specified.

(L v0) (R v0) (T v0) (Pair v0 v0) (Triple v0 v0 v0)
(L v1) (R v1) (T v1) (Pair v1 v1) (Triple v1 v1 v1)
(L v2) (R v2) (T v2) (Pair v2 v2) (Triple v2 v2 v2)
(L v3) (R v3) (T v3) (Pair v3 v3) (Triple v3 v3 v3)
(L v4) (R v4) (T v4) (Pair v4 v4) (Triple v4 v4 v4)
(L v5) (R v5) (T v5) (Pair v5 v5) (Triple v5 v5 v5)
(L v6) (R v6) (T v6) (Pair v6 v6) (Triple v6 v6 v6)
(L v7) (R v7) (T v7) (Pair v7 v7) (Triple v7 v7 v7)
(Src a) (Src b)
(Link a b) (Link b c)
(= (f $x) (wrapped $x))

!(msort (collapse (match &self (, (L $x) (R $y) (T $z) (Triple $x $y $z)) ($x $y $z))))
!(size-atom (collapse (match &self (, (L $x) (R $y) (Triple $x $y $z)) $z)))
!(msort (collapse (match &self (, (Pair $x $y) (L $x) (T $y)) $x)))
!(msort (collapse (match &self (, (L $x) (Triple $x $y $z)) (f $x))))
!(size-atom (collapse (match &self (, (L $x) (R $y)) ($x $y))))
!(collapse (match &self (, (L $x) (R v3) (Pair $x v3)) $x))
!(collapse (match &self (, (L v9) (R $y)) $y))
!(msort (collapse (let $b (match &self (, (Src $a) (Link $a $b)) $b) (let $u (add-atom &self (Link $b $b)) $b))))
!(msort (collapse (match &self (Link $x $y) ($x $y))))
