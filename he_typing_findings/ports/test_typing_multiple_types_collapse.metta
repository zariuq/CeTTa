; Ported from PeTTa examples/types.metta + recursive_types.metta.
; Coverage gap in the HE corpus: it declares multi-typed symbols but never
; ENUMERATES the intersection with collapse. Native HE supports this.
(: x Letter)
(: x Buchstabe)
!(assertEqualToResult (collapse (get-type x)) ((Letter Buchstabe)))

(: Metal Type) (: Sword Type) (: Paperclip Type) (: iron Metal)
(: blacksmith (-> Metal Sword))
(: blacksmith (-> Metal Paperclip))
; one application, both result types (intersection elimination)
!(assertEqualToResult (collapse (get-type (blacksmith iron))) ((Sword Paperclip)))
