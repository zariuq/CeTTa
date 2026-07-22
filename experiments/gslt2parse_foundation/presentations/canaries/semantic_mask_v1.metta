; Positive and negative boundaries for the finite semantic-mask compiler.
; The positive root retains one nested wrapper.  The negative root nests a
; second wrapper inside an active wrapper and therefore has no v1 lowering.

(gslt-presentation-v1 SemanticMaskCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule define-semantic-mask-positive
      (head
        (definition semantic-mask-positive
          (node semantic-mask-positive-root
            (right
              (char (cp 97))
              (node semantic-mask-wrapper (char (cp 98)))))))
      (body))
    (rule define-semantic-mask-nested
      (head
        (definition semantic-mask-nested
          (node semantic-mask-nested-root
            (node outer-wrapper
              (node inner-wrapper (char (cp 99)))))))
      (body))))
