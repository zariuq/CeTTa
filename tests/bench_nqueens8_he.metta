; N-Queens benchmark: find all solutions for 8 queens on 8x8 board.
; Expected: 92 solutions for 8-queens.
;
; Tests pure nondeterministic search with backtracking/pruning.
; Uses match on a column space for nondeterministic column choice,
; and case-with-no-matching-branch to kill failed branches (zero results).

; Column choices
!(bind! &cols8 (new-space))
!(add-atom &cols8 (col 1))
!(add-atom &cols8 (col 2))
!(add-atom &cols8 (col 3))
!(add-atom &cols8 (col 4))
!(add-atom &cols8 (col 5))
!(add-atom &cols8 (col 6))
!(add-atom &cols8 (col 7))
!(add-atom &cols8 (col 8))

; Check if placing a queen at $col is safe given already-placed queens.
; $placed is a flat list from cons-atom, $dist tracks diagonal distance.
(= (safe-col $col $placed $dist)
   (if (== $placed ())
       True
       (let ($head $tail) (decons-atom $placed)
         (if (== $col $head)
             False
             (if (== (abs-math (- $col $head)) $dist)
                 False
                 (safe-col $col $tail (+ $dist 1)))))))

; Place $n queens. Returns the placement list on success,
; produces zero results on failure (via case with no matching branch).
(= (place-queens $n $placed)
   (if (<= $n 0)
       $placed
       (match &cols8 (col $col)
         (case (safe-col $col $placed 1)
           ((True (place-queens (- $n 1) (cons-atom $col $placed))))))))

; Count all 8-queens solutions
!(println! "8-queens: counting solutions (expect 92)")
!(let $solutions (collapse (place-queens 8 ()))
   (size-atom $solutions))
