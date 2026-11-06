; Two answers for one input: the mode is relational, which the C target
; refuses.
(gslt-presentation-v1 NativeDirectRelationalV1
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
