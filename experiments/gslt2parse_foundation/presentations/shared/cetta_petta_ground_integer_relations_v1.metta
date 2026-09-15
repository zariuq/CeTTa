; CeTTa/PeTTa realization of the pure ground integer relation interface.
; Values have already passed the LanguageDef builtin-integer carrier check.

(gslt-presentation-v1 CettaPeTTaGroundIntegerRelationsV1
  (signature
    (operator metta-equation 2)
    (operator gslt:ground-integer-less 1)
    (operator gslt:ground-integer-not-less 1)
    (operator gslt:ground-integer-gap 1)
    (operator gslt:ground-integer-no-gap 1)
    (operator gslt:ground-unicode-scalar 1)
    (operator gslt:ground-non-unicode-scalar 1)
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2)
    (operator ground-integer-gap 2)
    (operator ground-integer-no-gap 2)
    (operator ground-unicode-scalar 1)
    (operator ground-non-unicode-scalar 1)
    (operator if 3)
    (operator < 2)
    (operator >= 2)
    (operator + 2)
    (operator quote 1)
    (operator metta-nullary 1))
  (equations)
  (rewrites
    (rule petta-ground-integer-less-v1
      (head
        (metta-equation
          (gslt:ground-integer-less
            (ground-integer-less ?left ?right))
          (if (< ?left ?right)
            (quote (ground-integer-less ?left ?right))
            (metta-nullary empty))))
      (body))
    (rule petta-ground-integer-not-less-v1
      (head
        (metta-equation
          (gslt:ground-integer-not-less
            (ground-integer-not-less ?left ?right))
          (if (>= ?left ?right)
            (quote (ground-integer-not-less ?left ?right))
            (metta-nullary empty))))
      (body))
    (rule petta-ground-integer-gap-v1
      (head
        (metta-equation
          (gslt:ground-integer-gap
            (ground-integer-gap ?left ?right))
          (if (< (+ ?left 1) ?right)
            (quote (ground-integer-gap ?left ?right))
            (metta-nullary empty))))
      (body))
    (rule petta-ground-integer-no-gap-v1
      (head
        (metta-equation
          (gslt:ground-integer-no-gap
            (ground-integer-no-gap ?left ?right))
          (if (>= (+ ?left 1) ?right)
            (quote (ground-integer-no-gap ?left ?right))
            (metta-nullary empty))))
      (body))
    (rule petta-ground-unicode-scalar-v1
      (head
        (metta-equation
          (gslt:ground-unicode-scalar
            (ground-unicode-scalar ?scalar))
          (if (< ?scalar 0)
            (metta-nullary empty)
            (if (< 1114111 ?scalar)
              (metta-nullary empty)
              (if (< ?scalar 55296)
                (quote (ground-unicode-scalar ?scalar))
                (if (< 57343 ?scalar)
                  (quote (ground-unicode-scalar ?scalar))
                  (metta-nullary empty)))))))
      (body))
    (rule petta-ground-non-unicode-scalar-v1
      (head
        (metta-equation
          (gslt:ground-non-unicode-scalar
            (ground-non-unicode-scalar ?scalar))
          (if (< ?scalar 0)
            (quote (ground-non-unicode-scalar ?scalar))
            (if (< 1114111 ?scalar)
              (quote (ground-non-unicode-scalar ?scalar))
              (if (< ?scalar 55296)
                (metta-nullary empty)
                (if (< 57343 ?scalar)
                  (metta-nullary empty)
                  (quote (ground-non-unicode-scalar ?scalar))))))))
      (body))))
