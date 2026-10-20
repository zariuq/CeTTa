!(import! &self json)

; A program may give the JSON constructors equations of its own.  The
; library never evaluates a JSON value: parsing, lookup, members and
; stringification see each value as it was parsed, and the operations still
; compose as ordinary calls.
(= (JsonNumberV1 $x) (Rewritten $x))
(= (JsonBoolV1 $x) (Rewritten $x))

!(json:inspect "[1, true]")
!(json:inspect-stringify (json:inspect "[1, true]"))
!(json:inspect-get-first (json:inspect "{\"k\": 5, \"k\": 6}") "k")
!(json:inspect-get-last (json:inspect "{\"k\": 5, \"k\": 6}") "k")
!(json:inspect-get-all (json:inspect "{\"k\": 5, \"k\": 6}") "k")
!(json:inspect-members (json:inspect "{\"k\": 5}"))
!(json:inspect-stringify (json:inspect "{\"nested\": [[1], {\"t\": true}]}"))
!(json:stringify (json:from-inspection (json:inspect "[1,true]")))

; A value the program passes through a variable is that program's value.
; HE interprets an argument at a parameter that is not Atom-typed, as
; upstream HE does (tests/he/untyped_parameter_interprets_data), so the
; program's equation applies there; PeTTa passes a variable's value on as
; it is.
!(let $v (json:inspect "[1]") (json:inspect-stringify $v))
