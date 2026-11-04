!(import! &self langdef)
; The term-language natives write a sequence (cons Head Tail) ... nil.  A
; PeTTa program builds such a sequence with cons, as it builds its lists,
; and takes the natives' sequences apart with cons patterns.
(= (first-point (cons (cp $n) $rest)) $n)
(= (rest-points (cons $h $rest)) $rest)
!(langdef:codepoints->symbol (cons (cp 167) (cons (cp 105) nil)))
!(first-point (langdef:symbol->codepoints §i))
!(rest-points (langdef:symbol->codepoints §i))
!(let (cons $h $t) (langdef:symbol->codepoints ab) (head $h))
!(case (langdef:symbol->codepoints ab) (((cons (cp $n) $t) (case-head $n)) ($o (other $o))))
!(langdef:codepoints->symbol (langdef:symbol->codepoints round-trip))
; A sequence stored as data reaches a native as the sequence it spells.
(stored (cons (cp 120) (cons (cp 121) nil)))
!(match &self (stored $s) (langdef:codepoints->symbol $s))
; and stays data to the program: a cons pattern meets the expression it is.
!(match &self (stored $s) (rest-points $s))
