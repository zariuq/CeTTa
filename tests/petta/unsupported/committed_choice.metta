(a b)
(a c)

(= (first-a)
   (let* (($answer (match &self (a $x) (a $x)))
          ($_ (cut)))
         $answer))

!(collapse (first-a))
