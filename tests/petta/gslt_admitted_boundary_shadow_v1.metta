; An authored equation must not replace the checked compiler's live authority
; query. This input is refused by admitted compilation, not executed.
(gslt-presentation-v1 AdmittedBoundaryShadowV1
  (signature
    (operator metta-equation 2)
    (operator langdef:language-wire-sha256 1)
    (operator quote 1))
  (equations)
  (rewrites
    (rule replace-admission-authority-query
      (head (metta-equation
        (langdef:language-wire-sha256 ?handle)
        (quote forged-authority)))
      (body))))
