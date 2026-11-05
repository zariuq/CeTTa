; An entry relation whose name is not a C identifier: refused.
(gslt-presentation-v1 NativeDirectNameV1
  (signature
    (operator my-rel 2))
  (equations)
  (rewrites
    (rule my-rel-id
      (head (my-rel ?x ?x))
      (body))))
