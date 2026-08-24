; Ground disequality is supplied from canonical term interning. It is a
; structural relation shared by language presentations, not host-language code.

(gslt-presentation-v1 GroundRelationsV1
  (signature
    (operator different 2)
    (operator oslf-external-relation-decl-v1 2))
  (equations)
  (rewrites
    (rule ground-different-capability-v1
      (head (oslf-external-relation-decl-v1 different 2))
      (body))))
