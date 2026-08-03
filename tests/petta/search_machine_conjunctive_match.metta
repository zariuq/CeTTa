(= (hide $value) (empty))

!(hide ((add-atom &self (edge a b))
        (add-atom &self (edge b c))
        (add-atom &self (edge a d))
        (add-atom &self (edge d e))
        (add-atom &self (edge noise nowhere))))

!(collapse
   (match &self (, (edge $source $middle)
                    (edge $middle $target))
          ($source $middle $target)))

!(collapse (match &self (,) seed))
