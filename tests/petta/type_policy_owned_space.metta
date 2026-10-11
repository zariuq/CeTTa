; bind! publishes no answer in both PeTTa profiles.
; The owner-specific call contracts below have the same observations.
!(bind! &pcg-left (new-space))
!(bind! &pcg-right (new-space))
!(add-atom &pcg-left (: pcg-owned (-> Number Atom)))
!(add-atom &pcg-left (= (pcg-owned $x) $x))
!(add-atom &pcg-right (: pcg-owned (-> Atom Atom)))
!(add-atom &pcg-right (= (pcg-owned $x) $x))
!(eval (pcg-owned (+ 2 4)) &pcg-left)
!(eval (pcg-owned (+ 2 4)) &pcg-right)
!(remove-atom &pcg-left (: pcg-owned (-> Number Atom)))
!(add-atom &pcg-left (: pcg-owned (-> Atom Atom)))
!(eval (pcg-owned (+ 2 4)) &pcg-left)
!(eval (pcg-owned (+ 2 4)) &pcg-right)
