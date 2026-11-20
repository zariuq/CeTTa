; Two answers for one input: a mode with several answers (nondet).
(gslt-presentation-v1 GsltCRelationalV1
  (signature
    (operator Amb 2)
    (operator One 0)
    (operator Two 0))
  (equations)
  (rewrites
    (rule amb-one
      (head (Amb ?x One))
      (body))
    (rule amb-two
      (head (Amb ?x Two))
      (body))))
