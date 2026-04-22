; Runtime add-atom/remove-atom should adapt Petta declarations like file load.

(= (f (42) 40.7 2)
   (42.7 42))

!(test (let $_ (add-atom &self (= (myfunc) 42))
          (let $value (reduce (myfunc))
             (let $_ (remove-atom &self (= (myfunc) 42))
                  $value)))
       42)

!(test (let $_ (add-atom &self (= (myfunc) 42))
          (let $_ (remove-atom &self (= (myfunc) 42))
             (collapse (match &self (= (myfunc) $x) True))))
       ())

!(test (let $_ (add-atom &self
                         (= (myfunc)
                            (match &self (= (f (42) 40.7 2) $x) $x)))
          (let $value (reduce (myfunc))
             (let $_ (remove-atom &self
                                  (= (myfunc)
                                     (match &self (= (f (42) 40.7 2) $x) $x)))
                  $value)))
       (42.7 42))
