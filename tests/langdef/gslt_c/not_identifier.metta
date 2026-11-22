; A relation whose name is no C identifier: names are data of the module.
(gslt-presentation-v1 GsltCNameV1
  (signature
    (operator my-rel 2))
  (equations)
  (rewrites
    (rule my-rel-id
      (head (my-rel ?x ?x))
      (body))))
