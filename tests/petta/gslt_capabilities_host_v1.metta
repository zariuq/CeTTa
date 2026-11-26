; A host theory for the capability gate: cap-double gives a value back to the
; rules (mode 10: the first argument in, the second out), and cap-zero gives
; one of two constants, which a rule can fix in its premise.
(gslt-presentation-v1 CapabilityHostV1
  (signature
    (operator cap-double 2)
    (operator cap-zero 2)
    (operator gslt:cap-double 1)
    (operator gslt:cap-zero 1)
    (operator oslf-external-relation-decl-v1 2)
    (operator oslf-external-relation-modes-v1 2)
    (operator metta-equation 2)
    (operator let 3)
    (operator if 3)
    (operator == 2)
    (operator * 2)
    (operator quote 1))
  (equations)
  (rewrites
    (rule cap-double-capability (head (oslf-external-relation-decl-v1 cap-double 2)) (body))
    (rule cap-double-modes (head (oslf-external-relation-modes-v1 cap-double "10")) (body))
    (rule cap-double-petta
      (head (metta-equation (gslt:cap-double (cap-double ?x ?y))
                            (let ?d (* ?x 2) (quote (cap-double ?x ?d)))))
      (body))
    (rule cap-zero-capability (head (oslf-external-relation-decl-v1 cap-zero 2)) (body))
    (rule cap-zero-modes (head (oslf-external-relation-modes-v1 cap-zero "10")) (body))
    (rule cap-zero-petta
      (head (metta-equation (gslt:cap-zero (cap-zero ?x ?z))
                            (let ?k (if (== ?x 0) ZeroV1 NonzeroV1) (quote (cap-zero ?x ?k)))))
      (body))))
