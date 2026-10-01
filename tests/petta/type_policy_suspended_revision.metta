; The outer call has selected two declaration plans before its argument
; replaces them and a nested typed call observes the new generation.
(: pcg-live (-> Number Number))
(: pcg-live (-> Number Atom))
(= (pcg-live $x) $x)
(: pcg-poke (-> Number Atom))
(= (pcg-poke $x) $x)
(= (pcg-replace)
   (let $_ (remove-atom &self (: pcg-live (-> Number Number)))
     (let $_ (remove-atom &self (: pcg-live (-> Number Atom)))
       (let $_ (add-atom &self (: pcg-live (-> Atom Atom)))
         (pcg-poke 6)))))
!(collapse (pcg-live (pcg-replace)))
!(pcg-live (+ 2 4))
