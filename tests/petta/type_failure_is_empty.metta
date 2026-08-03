; PeTTa's relation-level type incompatibility prunes a branch.  It is not an
; observable HE-style Error value.
(: petta:typed (-> Type1 Type1))
(: input-ok Type1)
(: input-bad Type2)
(: output-bad Type2)

(= (petta:typed $value)
   (if (== $value input-ok)
       input-ok
       output-bad))

!(test (petta:typed input-ok) input-ok)
!(test (petta:typed input-bad) ())
