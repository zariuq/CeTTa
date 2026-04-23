(= (fib $N)
   (if (< $N 2)
       $N
       (+ (fib (- $N 1))
          (fib (- $N 2)))))

(= (myfunc)
   5)

!(assertEqualToResult
  (call (fib (myfunc)))
  (5))
