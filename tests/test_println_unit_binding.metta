; HE-backed runtime syntax: println! returns unit, so binding against ()
; should continue through let*.

!(let* (
    (() (println! "bind-unit"))
    ($x ok)
  )
  $x)
