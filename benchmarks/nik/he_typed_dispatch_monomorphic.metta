(: typed-choice (-> Number Number))
(= (typed-choice $value) $value)

(= (typed-dispatch-loop $n)
   (if (== $n 0)
       Done
       (let $value (typed-choice 7)
         (typed-dispatch-loop (- $n 1)))))

!(typed-dispatch-loop 20000)
