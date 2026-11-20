; A rule that builds a list: cons and nil are constructors like any other.
(gslt-presentation-v1 GsltCBuildsConsV1
  (signature
    (operator Mk 2)
    (operator cons 2)
    (operator nil 0))
  (equations)
  (rewrites
    (rule mk
      (head (Mk ?x (cons ?x nil)))
      (body))))
