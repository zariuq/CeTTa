(= (compile-phase-fib)
   (let $temp (add-atom &self (= (phase-fib $N)
                                 (if (< $N 2)
                                     $N
                                     (+ (phase-fib (- $N 1))
                                        (phase-fib (- $N 2))))))
     ((within (phase-fib 5))
      (call-within (call (phase-fib 5)))
      (quote-within (quote (phase-fib 5)))
      (eval-within (eval (phase-fib 5)))
      (reduce-within (reduce (phase-fib 5))))))

!(test (compile-phase-fib)
       ((within (quote (phase-fib 5))) (call-within 5) (quote-within (quote (phase-fib 5))) (eval-within 5) (reduce-within 5)))
