(= (long-logical-list $n)
   (if (== $n 0)
       ()
       (cons $n (long-logical-list (- $n 1)))))

!(import_prolog_function length)
!(length (long-logical-list 129))
