; A snapshot an atom of a row still holds outlives the request that made it,
; and keeps the view it froze.
!(bind! &escape (new-space))
!(add-atom &escape (item a))
!(with-space-snapshot $snap &escape (add-atom &self (kept $snap)))
!(add-atom &escape (item b))
!(match &self (kept $s) (collapse (match $s (item $x) $x)))
!(match &escape (item $x) $x)
!(match &self (kept $s) (collapse (match $s (item $x) $x)))
