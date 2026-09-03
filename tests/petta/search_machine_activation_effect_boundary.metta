(= (effectful-countdown $n)
   (if (== $n 0)
       true
       (let $_write (add-atom &self (countdown-step $n))
            (effectful-countdown (- $n 1)))))

!(effectful-countdown 3)
