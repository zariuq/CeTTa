; CeTTa/HE realization of the pure ground relation interface.
; An HE equation hands its result back as data: a result constructor
; declared over Atom domains is not entered, so no quote wrapper is needed.
; The two sides are compared as written, neither reduced, by noreduce-eq:
; HE's standard library defines it, and Prime, whose library spellings run
; this realization, reads it natively under its sealed quotation.

(gslt-presentation-v1 CettaHEGroundRelationsV1
  (signature
    (operator metta-equation 2)
    (operator gslt:different 1)
    (operator different 2)
    (operator if 3)
    (operator noreduce-eq 2)
    (operator metta-nullary 1))
  (equations)
  (rewrites
    (rule he-ground-different-v1
      (head
        (metta-equation
          (gslt:different (different ?left ?right))
          (if (noreduce-eq ?left ?right)
            (metta-nullary empty)
            (different ?left ?right))))
      (body))))
