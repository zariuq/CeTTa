; Pure ground relations used when a LanguageDef carries builtin integers.
; The positive and complementary relations partition typed integer inputs.

(gslt-presentation-v1 GroundIntegerRelationsV1
  (signature
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2)
    (operator ground-integer-gap 2)
    (operator ground-integer-no-gap 2)
    (operator ground-unicode-scalar 1)
    (operator ground-non-unicode-scalar 1)
    (operator oslf-external-relation-decl-v1 2))
  (equations)
  (rewrites
    (rule ground-integer-less-capability-v1
      (head (oslf-external-relation-decl-v1 ground-integer-less 2))
      (body))
    (rule ground-integer-not-less-capability-v1
      (head (oslf-external-relation-decl-v1 ground-integer-not-less 2))
      (body))
    (rule ground-integer-gap-capability-v1
      (head (oslf-external-relation-decl-v1 ground-integer-gap 2))
      (body))
    (rule ground-integer-no-gap-capability-v1
      (head (oslf-external-relation-decl-v1 ground-integer-no-gap 2))
      (body))
    (rule ground-unicode-scalar-capability-v1
      (head (oslf-external-relation-decl-v1 ground-unicode-scalar 1))
      (body))
    (rule ground-non-unicode-scalar-capability-v1
      (head (oslf-external-relation-decl-v1 ground-non-unicode-scalar 1))
      (body))))
