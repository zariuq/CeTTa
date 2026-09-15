(gslt-presentation-v1 TptpFirstOrderDocumentDuplicateIffV1
  (signature
    (operator metta-equation 2)
    (operator node 2)
    (operator pair 2)
    (operator tptp-doc:formula 2)
    (operator tptp-fo:formula-iff 2))
  (equations)
  (rewrites
    (rule duplicate-formula-iff
      (head (metta-equation
        (tptp-doc:formula (node iff (pair ?left ?right)) ?scope)
        (tptp-fo:formula-iff
          (tptp-doc:formula ?left ?scope)
          (tptp-doc:formula ?right ?scope))))
      (body))))
