; The byte escape of CeTTa's extended HE profiles projects every byte, \x00 to
; \xff; the base projection keeps the upstream bound of \x7f.  Composed over
; CeTTaHEAtomProjectionV1.
(gslt-presentation-v1 CeTTaHEByteEscapeProjectionExtensionV1
  (signature
    (operator sexpr-wrapper-hex 6))
  (equations)
  (rewrites
    (rule sexpr-wrapper-hex-cetta-he-byte-escape
      (head
        (sexpr-wrapper-hex
          cetta-he-v1 byte-escape 2 2 255 16))
      (body))
  ))
