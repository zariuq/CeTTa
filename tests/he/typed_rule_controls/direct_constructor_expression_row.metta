; A checked constructor's application is a value where HE's evaluation
; accepts it: through the constructor's function type.  A type row on the
; application itself does not make ill-typed fields well typed, at the top
; level or nested, though get-type still reports the row.
(: DirectAnnotatedBox (-> Number DirectAnnotatedRange))
(: (DirectAnnotatedBox "wrong-field") DirectAnnotatedRange)
!(collapse (DirectAnnotatedBox "wrong-field"))
!(get-type (DirectAnnotatedBox "wrong-field"))
(: wrap (-> DirectAnnotatedRange WrapRange))
!(collapse (wrap (DirectAnnotatedBox "wrong-field")))
; Positive controls: a row the constructor's type agrees with, no row, and
; a row on an application whose head has no function type.
(: (DirectAnnotatedBox 1) DirectAnnotatedRange)
!(DirectAnnotatedBox 1)
!(DirectAnnotatedBox 2)
!(wrap (DirectAnnotatedBox 3))
(: c C)
(: d D)
(: (c d) (C D))
(: cd (-> (C D) Number))
(= (cd $x) 9)
!(cd (c d))
; The ill-typed build at the top level, last: HE stops at a top-level error.
!(DirectAnnotatedBox "wrong-field")
