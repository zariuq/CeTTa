; The match binds $x and $y. The template is the ground atom yes.
; The continuation must still read those bindings from the branch image.

(left a)
(left b)
(right a old)
(right b other)

!(let $_ (match &self (, (left $x) (right $x $y)) yes)
   (out $x $y))
