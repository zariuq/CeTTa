!(import! &self ../../lib/lib_wmpln)

; Runnable shadow for exact-denominator counting-capacity rational cuts.
;
; Lean surface:
;   FuzzyCapacity.countingCapacity_ge_nat_div_iff_card_ge_of_card_eq
;   FuzzyCapacity.nat_div_le_one_sub_countingCapacity_iff_card_add_le_of_card_eq
;   predicateCountingCapacityCardinalityBandCut
;
; Hand model: a four-object finite carrier, with `hits` objects in ext(P).
;   countingCapacity(ext P) = hits / 4
;   1 - countingCapacity(ext P) = (4 - hits) / 4
;
; Therefore:
;   k / 4 <= countingCapacity(ext P)     iff k <= hits
;   k / 4 <= 1 - countingCapacity(ext P) iff hits + k <= 4
; and the finite-frequency band is their conjunction.

(= (absdiff $x $y) (if (< $x $y) (- $y $x) (- $x $y)))
(= (approx $x $y) (< (absdiff $x $y) 0.000001))
(= (ge? $threshold $score) (or (< $threshold $score) (approx $threshold $score)))

(= (counting-score4 $hits) (/ $hits 4.0))
(= (counting-complement-score4 $hits) (- 1.0 (counting-score4 $hits)))
(= (threshold4 $k) (/ $k 4.0))

(= (lower-cut-event4 $k $hits)
   (ge? (threshold4 $k) (counting-score4 $hits)))
(= (lower-card-event $k $hits)
   (<= $k $hits))

(= (complement-cut-event4 $kMissing $hits)
   (ge? (threshold4 $kMissing) (counting-complement-score4 $hits)))
(= (upper-card-event4 $kMissing $hits)
   (<= (+ $hits $kMissing) 4))

(= (counting-band-event4 $kLower $kMissing $hits)
   (and (lower-cut-event4 $kLower $hits)
        (complement-cut-event4 $kMissing $hits)))
(= (cardinality-band-event4 $kLower $kMissing $hits)
   (and (lower-card-event $kLower $hits)
        (upper-card-event4 $kMissing $hits)))

; Positive finite-band case:
; hits=3, N=4, kLower=2, kMissing=1.
;   score = 3/4, complement = 1/4.
;   2/4 <= 3/4 and 1/4 <= 1/4, so the band holds.
!(assertEqual (approx (counting-score4 3) 0.75) True)
!(assertEqual (approx (counting-complement-score4 3) 0.25) True)
!(assertEqual (lower-cut-event4 2 3) True)
!(assertEqual (lower-card-event 2 3) True)
!(assertEqual (complement-cut-event4 1 3) True)
!(assertEqual (upper-card-event4 1 3) True)
!(assertEqual (counting-band-event4 2 1 3) True)
!(assertEqual (cardinality-band-event4 2 1 3) True)

; Lower side fails:
; hits=1 is below kLower=2, even though there is plenty of complement mass.
!(assertEqual (approx (counting-score4 1) 0.25) True)
!(assertEqual (approx (counting-complement-score4 1) 0.75) True)
!(assertEqual (lower-cut-event4 2 1) False)
!(assertEqual (lower-card-event 2 1) False)
!(assertEqual (complement-cut-event4 1 1) True)
!(assertEqual (upper-card-event4 1 1) True)
!(assertEqual (counting-band-event4 2 1 1) False)
!(assertEqual (cardinality-band-event4 2 1 1) False)

; Upper/complement side fails:
; hits=4 has enough support, but no room for kMissing=1 complement mass.
!(assertEqual (approx (counting-score4 4) 1.0) True)
!(assertEqual (approx (counting-complement-score4 4) 0.0) True)
!(assertEqual (lower-cut-event4 2 4) True)
!(assertEqual (lower-card-event 2 4) True)
!(assertEqual (complement-cut-event4 1 4) False)
!(assertEqual (upper-card-event4 1 4) False)
!(assertEqual (counting-band-event4 2 1 4) False)
!(assertEqual (cardinality-band-event4 2 1 4) False)

; Equality boundary stays on the passing side:
; hits=2, kLower=2, kMissing=2 gives score=complement=1/2.
!(assertEqual (approx (counting-score4 2) 0.5) True)
!(assertEqual (approx (counting-complement-score4 2) 0.5) True)
!(assertEqual (lower-cut-event4 2 2) True)
!(assertEqual (lower-card-event 2 2) True)
!(assertEqual (complement-cut-event4 2 2) True)
!(assertEqual (upper-card-event4 2 2) True)
!(assertEqual (counting-band-event4 2 2 2) True)
!(assertEqual (cardinality-band-event4 2 2 2) True)

; Out-of-range guardrail:
; k=5 asks for threshold 5/4, impossible on a normalized score or complement.
!(assertEqual (approx (threshold4 5) 1.25) True)
!(assertEqual (lower-cut-event4 5 4) False)
!(assertEqual (lower-card-event 5 4) False)
!(assertEqual (complement-cut-event4 5 0) False)
!(assertEqual (upper-card-event4 5 0) False)

; Exact-denominator warning:
; the same raw hit count has different meaning if the carrier size changes.
; With hits=2, the four-object score is 1/2; if it were a two-object carrier,
; the score would be 1.  The Lean theorem's exact-cardinality hypothesis is
; therefore load-bearing, not cosmetic.
!(assertEqual (approx (counting-score4 2) 0.5) True)
!(assertEqual (approx (/ 2 2.0) 1.0) True)
!(assertEqual (approx (counting-score4 2) (/ 2 2.0)) False)

; Proper all-HOL band / strict-capacity readout:
; Lean surface:
;   FuzzyCapacity.countingCapacity_pos_and_lt_one_iff_proper_cardinality_of_card_eq
;   predicateCountingCapacityExistsAndExistsNotBandCut_ge_iff_countingCapacity_pos_and_lt_one
;
; For N=4, the all-HOL formula (exists x, P x) and (exists x, not P x)
; represents exactly the open numeric event:
;   0 < countingCapacity(ext P) < 1
; i.e. at least one hit and at least one miss.
(= (strict-capacity-proper-event4 $hits)
   (and (< 0.0 (counting-score4 $hits))
        (< (counting-score4 $hits) 1.0)))
(= (proper-cardinality-event4 $hits)
   (and (<= 1 $hits) (<= (+ $hits 1) 4)))

; Negative lower boundary: empty extension has capacity 0, so the strict event
; and the proper all-HOL band both reject.
!(assertEqual (approx (counting-score4 0) 0.0) True)
!(assertEqual (strict-capacity-proper-event4 0) False)
!(assertEqual (proper-cardinality-event4 0) False)
!(assertEqual (counting-band-event4 1 1 0) False)

; Positive interior cases: one, two, or three hits are all strictly between
; zero and one, and exactly match the proper cardinality/band event.
!(assertEqual (strict-capacity-proper-event4 1) True)
!(assertEqual (proper-cardinality-event4 1) True)
!(assertEqual (counting-band-event4 1 1 1) True)
!(assertEqual (== (strict-capacity-proper-event4 1)
                  (counting-band-event4 1 1 1))
              True)

!(assertEqual (strict-capacity-proper-event4 2) True)
!(assertEqual (proper-cardinality-event4 2) True)
!(assertEqual (counting-band-event4 1 1 2) True)
!(assertEqual (== (strict-capacity-proper-event4 2)
                  (counting-band-event4 1 1 2))
              True)

!(assertEqual (strict-capacity-proper-event4 3) True)
!(assertEqual (proper-cardinality-event4 3) True)
!(assertEqual (counting-band-event4 1 1 3) True)
!(assertEqual (== (strict-capacity-proper-event4 3)
                  (counting-band-event4 1 1 3))
              True)

; Negative upper boundary: full extension has capacity 1, so ordinary support
; is maximal but the proper all-HOL band rejects because there is no miss.
!(assertEqual (approx (counting-score4 4) 1.0) True)
!(assertEqual (strict-capacity-proper-event4 4) False)
!(assertEqual (proper-cardinality-event4 4) False)
!(assertEqual (counting-band-event4 1 1 4) False)
