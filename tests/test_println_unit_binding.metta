; HE-backed runtime surface: println! returns unit, so binding against ()
; should continue through let*.

!(let* (
    (() (println! "bind-unit"))
    ($x ok)
  )
  $x)
