; Extend a standard classifier with an ordinary MeTTa equation.
(= (get-type $x)
   (if (== (% $x 2) 0) EvenNumber (empty)))
(: even-add (-> EvenNumber EvenNumber EvenNumber))
(= (even-add $x $y) (+ $x $y))

!(collapse (get-type 2))
!(even-add 2 4)
!(even-add 2 3)
!(std:get-type 2)

; Inspecting originals returns definitions as data. No evaluation of their
; open variables is required, and this call does not change the program.
!(size-atom (collapse (std:definitions get-type)))

; Remove the original declarations and delegates, leaving the extension.
!(let $definition (std:definitions get-type)
   (remove-atom &self $definition))
!(get-type 2)
!(std:get-type 2)

; Restore by re-adding that same data. The extension remains and retains
; its earlier position among the alternatives. Repeated additions are real
; duplicate occurrences; restoration does not silently deduplicate them.
!(let $definition (std:definitions get-type)
   (add-atom &self $definition))
!(collapse (get-type 2))
!(even-add 2 4)
