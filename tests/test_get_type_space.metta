; HE-backed type lookup relative to an explicit space: the op returns all
; matching types from the chosen space and %Undefined% when none are found.

(: foo TypeA)
(: foo TypeB)
!(get-type-space &self foo)
!(get-type-space &self baz)
!(bind! &kb (new-space))
!(add-atom &kb (: foo TypeA))
!(add-atom &kb (: foo TypeB))
!(get-type-space &kb foo)
