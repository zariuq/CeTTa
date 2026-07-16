; CAPABILITY GAP (documented, not a working port): PeTTa lets a user override
; get-type with a computed rule (examples/types_dependent.metta:
;   (= (get-type $x) (if (== (% $x 2) 0) EvenNumber ...)) ).
; In HE, get-type is a sealed grounded builtin: the equation is IGNORED and the
; builtin type wins. This test pins that HE ignores the override.
(= (get-type myatom) MyComputedType)
!(assertEqual (get-type myatom) %Undefined%)
