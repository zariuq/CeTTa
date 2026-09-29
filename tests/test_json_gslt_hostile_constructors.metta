!(import! &self json)

; A program may give the JSON constructors equations of its own.  The
; library never evaluates a JSON value: parsing, lookup, members and
; stringification see each value as it was parsed, and the operations still
; compose as ordinary calls.
(= (JsonNumberV1 $x) (Rewritten $x))
(= (JsonBoolV1 $x) (Rewritten $x))

!(json:parse "[1, true]")
!(json:stringify (json:parse "[1, true]"))
!(json:get-first (json:parse "{\"k\": 5, \"k\": 6}") "k")
!(json:get-last (json:parse "{\"k\": 5, \"k\": 6}") "k")
!(json:get-all (json:parse "{\"k\": 5, \"k\": 6}") "k")
!(json:members (json:parse "{\"k\": 5}"))
!(json:stringify (json:parse "{\"nested\": [[1], {\"t\": true}]}"))

; A value the program passes through a variable is that program's value.
; HE interprets an argument at a parameter that is not Atom-typed, as
; upstream HE does (tests/he/untyped_parameter_interprets_data), so the
; program's equation applies there; PeTTa passes a variable's value on as
; it is.
!(let $v (json:parse "[1]") (json:stringify $v))
