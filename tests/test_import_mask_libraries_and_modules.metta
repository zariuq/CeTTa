; Each library and each native module answers to its own import: importing
; the integer library makes no pattern-module word available, and importing
; the pattern module afterwards loads it in full.
!(bind! &facts (new-space))
!(add-atom &facts (edge a b))
!(import! &self integer)
!(integer:and 6 3)
; not imported yet: the word stays as written
!(pat:match% facts (edge a $y) $y)
!(import! &self pat)
!(collapse (pat:match% &facts (edge a $y) $y))
!(integer:or 4 1)
