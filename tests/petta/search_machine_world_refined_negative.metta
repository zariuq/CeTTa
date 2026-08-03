(= (find $space $pattern)
   (case (match $space $pattern true)
         ((true true)
          (Empty false))))

!(if (find &self (absent $a $b))
     (found $a $b)
     missing)
