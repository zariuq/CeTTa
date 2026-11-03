; An error raised while bind!'s value is evaluated is bind!'s answer, and the
; name stays unbound: an error is never swallowed into a binding.  The same
; error reached through `let` is the answer too.
!(bind! &raised (car-atom ()))
!(get-type &raised)
!(let $x (car-atom ()) (bind! &via-let $x))

; Negative: an Error term written as the value is data, and is bound; an
; ordinary value is bound as before.
!(bind! &data (Error kept as-data))
!(get-type &data)
!(bind! &ok 5)
!(+ &ok 1)
