; The other order: the pattern module first, then the integer and float
; libraries, each loaded in full and working.
!(bind! &facts (new-space))
!(add-atom &facts (edge a b))
!(import! &self pat)
!(import! &self integer)
!(import! &self float)
!(integer:xor 5 -1)
!(float:is-nan 1.5)
!(collapse (pat:match% &facts (edge a $y) $y))
