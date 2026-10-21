!(import! &self json)

; An arbitrary term cannot masquerade as an operational JSON number.
!(json:stringify (InvalidJsonScalarV1 "01"))
