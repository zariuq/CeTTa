; The same error without a catch ends the run with PeTTa's message.
(: red Colour)
(: shade Shade)
(: tint (-> Colour Shade))
(= (tint $c) (foldl-atom (1) shade $acc $item $c))

!(eval (quote (tint red)))
