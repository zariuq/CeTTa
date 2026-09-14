!(msort
   (collapse
      (hyperpose
         ((let $_ (add-atom &self (= (explicit-eval-fresh) seen))
             (eval (explicit-eval-fresh)))
          companion))))
