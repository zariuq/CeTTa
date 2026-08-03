(= (find $space $pattern)
   (case (match $space $pattern True)
         ((True True)
          (Empty False))))

(friend a b)

!(if (find &self (friend $a $b))
     (found $a $b)
     missing)
