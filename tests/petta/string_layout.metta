; Strings print as PeTTa's swrite prints them: quotes and backslashes are
; escaped, while newlines and tabs stay as they are.  repr returns the same
; text, and the reader takes a raw newline inside a string back.
!(println! (text "a\nb"))
!(println! (repr "a\nb"))
!(println! (repr (repr "q")))
!(println! (repr "back\\slash"))
!(println! (repr "tab\tx"))
!(println! (repr (x "a\nb" 1.5)))
!(println! (sread (repr (x "a\nb"))))
