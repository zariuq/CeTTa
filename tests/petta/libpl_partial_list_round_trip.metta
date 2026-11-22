; A partial list read back from Prolog term text: atom_to_term gives a list
; cell whose tail is not a list, unbound or another term, and repra writes it
; again, so the cell, its variables and their sharing survive the round trip.
; The answers are SWI-PeTTa's.
!(import_prolog_function atom_to_term)
(= (read-text $text) (let $_ (atom_to_term $text $value) $value))
!(read-text "[1|2]")
!(read-text "[1,2|3]")
!(read-text "[a|[b|[]]]")
!(test (read-text "[1|X]") (cons 1 $tail))
!(test (read-text "[parts,[A|B],A,B]") (let $c (cons $h $t) (parts $c $h $t)))
!(test (read-text (repra (cons 1 2))) (cons 1 2))
!(test (read-text (repra (cons a (cons b c)))) (cons a (cons b c)))
(= (open-cell-and-parts) (let $cell (cons $head $tail) (parts $cell $head $tail)))
!(test (read-text (repra (open-cell-and-parts))) (open-cell-and-parts))
