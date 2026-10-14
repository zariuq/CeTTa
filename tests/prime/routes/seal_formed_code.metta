; The seal lives in the term.  A mention inside a quotation, of a name a
; pattern binds at a lower quote depth, is the quotation's own variable, so
; no substitution reaches it.  Code formed while the program runs is sealed
; exactly as code the reader formed: from text by parse, by lift app, and a
; binder built from parts by cons-atom.  A crossing set lets a name into the
; quotation instead.

; ---- Read from this file.
!(row reader)
!(let $c @(let $x 5 @$x) *$c)
!(row reader-crossing)
!(let $c @(let $x 5 @$x{$x}) *$c)

; ---- Formed by lift app: the same code as the row above it.
!(row lift-app)
!(let $c (lift app @let @$x @5 @@$x) *$c)
; Pieces built apart, which no reading related: the built let seals its
; pattern's name in the quoted body.
!(row lift-app-pieces)
!(let $p (cons-atom quote ($x))
   (let $b (cons-atom quote ((quote $x)))
     (let $c (lift app @let $p @5 $b) *$c)))

; ---- Formed from text by parse.
!(row parse-lift)
!(let $c (lift (parse "(let $x 5 @$x)")) *$c)
!(row parse-held)
!(let $p (parse "(let $x 5 @$x)") $p)
!(row parse-crossing)
!(let $p (parse "(let $x 5 @$x{$x})") $p)

; ---- A binder built from parts by cons-atom.
!(row cons-let)
!(let $t (cons-atom let ($x 5 (quote $x))) $t)
!(row cons-case)
!(let $t (cons-atom case (5 (($x (quote $x))))) $t)
; Not a binder: nothing is sealed, nothing binds.
!(row cons-data)
!(cons-atom f ($x (quote $x)))
