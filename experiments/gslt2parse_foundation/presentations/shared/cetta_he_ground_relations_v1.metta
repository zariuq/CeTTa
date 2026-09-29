; CeTTa/HE realization of the pure ground relation interface.
; An HE equation hands its result back as data: a result constructor
; declared over Atom domains is not entered, so no quote wrapper is needed.

(gslt-presentation-v1 CettaHEGroundRelationsV1
  (signature
    (operator metta-equation 2)
    (operator gslt:different 1)
    (operator different 2)
    (operator if 3)
    (operator == 2)
    (operator quote 1)
    (operator metta-nullary 1))
  (equations)
  (rewrites
    (rule he-ground-different-v1
      (head
        (metta-equation
          (gslt:different (different ?left ?right))
          (if (== (quote ?left) (quote ?right))
            (metta-nullary empty)
            (different ?left ?right))))
      (body))))
