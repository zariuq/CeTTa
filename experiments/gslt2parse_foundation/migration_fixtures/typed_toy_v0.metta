; Test-only input for the pinned v0-to-v1 provenance migration. The second z
; below violates the decorative v0 B annotation, demonstrating why migration
; preserves the actual one-carrier Horn term rather than trusting those sorts.

(presentation TypedToyV0
  (types
    (type A)
    (type B)
    (type Goal))
  (terms
    (constructor z A)
    (constructor pair A B B)
    (constructor rel B Goal))
  (equations)
  (rewrites
    (rule rel-z
      (head (rel (pair z z)))
      (body))))
