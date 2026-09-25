; swrite is PeTTa's repr text, and it is UTF-8 text: a non-ASCII character
; is one character, not one per byte, and newlines stay raw.
!(import_prolog_function swrite)
!(println! (swrite "a—b ř ⋄"))
!(test (progn (translatePredicate (string_length (swrite "a—b") $n)) $n) 5)
!(println! (swrite (x "a\nb" 1.5 sym)))
!(test (== (swrite "ř") (repr "ř")) true)
