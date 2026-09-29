; The byte escape of CeTTa's extended HE profiles: \x followed by two hex
; digits names one byte of the string, from \x00 to \xff.  The base reader keeps
; the upstream bound of \x7f, below which a byte and its scalar coincide.
(gslt-presentation-v1 HEReaderByteEscapeExtensionV1
  (signature)
  (equations)
  (rewrites
    (rule def-he-byte-escape (head (definition he-byte-escape (node byte-escape (right (lit (cons (cp 92) (cons (cp 120) nil))) (seq (class he-hex-scalar) (class he-hex-scalar)))))) (body))))
