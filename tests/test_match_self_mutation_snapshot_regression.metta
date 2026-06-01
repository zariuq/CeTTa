; A match result is a snapshot: mutating the matched space while consuming
; the result must not make the same match chase newly-added atoms.

!(add-atom &self (snapshot-seed 0))

!(case (match &self (snapshot-seed $x) $x)
       (($x (add-atom &self (snapshot-seed (+ $x 1))))))

!(assertEqual
  (collapse (match &self (snapshot-seed $x) $x))
  (0 1))
