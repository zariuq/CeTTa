(= (space) my_space_name)
(= (payload) reduced)

!(add-atom (space) (payload))
!(test (match (space) $atom $atom) (quote (payload)))

; An authored location token is data until a space-consuming operation
; resolves it.  Literal location patterns must therefore match before any
; registry payload is consulted.
(: eval-in-self (-> Atom %Undefined% %Undefined%))
(= (eval-in-self $expression &self)
   (eval $expression))
!(test (eval-in-self (+ 5 5) &self) 10)
