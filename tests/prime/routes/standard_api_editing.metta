; Originals are ordinary data, available independently of local copies.
!(size-atom (collapse (std:definitions get-type)))
!(std:get-type 2)
(= (get-type 2) EvenNumber)
!(collapse (get-type 2))
(= (get-type 2) EvenNumber)
!(collapse (get-type 2))
!(remove-atom &self (= (get-type 2) EvenNumber))
!(collapse (get-type 2))

; Remove just the original rows, preserving the authored extension.
!(let $definition (std:definitions get-type)
   (remove-atom &self $definition))
!(get-type 2)
!(get-type 3)
!(std:get-type 3)

; Re-add the originals. Their new occurrence order follows the user clause.
!(let $definition (std:definitions get-type)
   (add-atom &self $definition))
!(collapse (get-type 2))
!(get-type 3)

; Ordinary library functions have the same inspect/remove/re-add workflow.
!(let $definition (std:definitions noeval)
   (remove-atom &self $definition))
!(noeval 9)
!(std:noeval 9)
!(let $definition (std:definitions noeval)
   (add-atom &self $definition))
!(noeval 9)
!(collapse (std:definitions unknown-api))
