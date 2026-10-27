; A set space holds each atom once: adding an atom it holds, or an
; alpha-variant of one, changes nothing.  A bag space keeps every copy.
!(bind! &s (new-space set))
!(bind! &b (new-space))
!(add-atom &s (edge 1 2))
!(add-atom &s (edge 1 2))
!(add-atom &s (edge 2 3))
!(add-atom &s (p $x))
!(add-atom &s (p $y))
!(add-atom &s (p 1))
!(add-atom &b (edge 1 2))
!(add-atom &b (edge 1 2))

; Ground duplicates and alpha-variants are held once; the bag holds both.
!(let $bag (collapse (match &s (edge $a $b) ($a $b))) (sort-atom $bag))
!(let $held (collapse (match &s (p $z) p)) (size-atom $held))
!(let $held (collapse (match &b (edge 1 2) yes)) (size-atom $held))

; Removing takes the atom out, and adding it again holds one copy.
!(remove-atom &s (edge 1 2))
!(collapse (match &s (edge 1 2) yes))
!(add-atom &s (edge 1 2))
!(add-atom &s (edge 1 2))
!(collapse (match &s (edge 1 2) yes))

; Every held atom is answered once.
!(let $held (collapse (get-atoms &s)) (size-atom $held))
