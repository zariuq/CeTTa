(= (compiled-cons $tail)
   (quote (cons 42 $tail)))

!(add-translator-rule! compiled-cons)
!(compiled-cons (43))
