; The C target reads cons lists from its input only: a rule that builds one
; is refused.
(gslt-presentation-v1 NativeDirectBuildsConsV1
  (signature
    (operator Mk 2)
    (operator cons 2)
    (operator nil 0))
  (equations)
  (rewrites
    (rule mk
      (head (Mk ?x (cons ?x nil)))
      (body))))
