; A ground partial is a PeTTa value, not syntax to execute.  The specialized
; recursive closure may carry it through the shared prepared machine without
; repeatedly rediscovering the same clause and substitution plan.
(= (prepared-specialized-loop $f $n)
   (if (== $n 0)
       0
       (+ ($f $n)
          (prepared-specialized-loop $f (- $n 1)))))
!(test (prepared-specialized-loop (+ 1) 10000) 50015000)

; `%` denotes floor modulo in PeTTa, following the pinned SWI-PeTTa
; composition.  Test the grounded surface and the generated register program
; on the sign cases which distinguish modulo from truncating remainder.
!(% -17 5)
!(% 17 -5)
!(% -17 -5)
(= (prepared-specialized-mod $a $b) (% $a $b))
!(test (prepared-specialized-mod -17 5) 3)
!(test (prepared-specialized-mod 17 -5) -3)
!(test (prepared-specialized-mod -17 -5) -2)

; A fused recursive branch remains subordinate to its enclosing search
; control.  The expensive losing branch must observe the cancellation issued
; when the cheap branch satisfies `once`.
(= (prepared-find-divisor $n $d)
   (if (> (* $d $d) $n)
       $n
       (if (== 0 (% $n $d))
           $d
           (prepared-find-divisor $n (+ $d 1)))))
(= (prepared-prime? $n)
   (== $n (prepared-find-divisor $n 2)))
!(once (hyperpose ((prepared-prime? 535372570000000063)
                   (prepared-prime? 5421844300001))))

; Quote is another opaque PeTTa value.  Its payload must remain syntax even
; when the enclosing specialized relation is compiled as a pure program.
(= (prepared-specialized-quote $f $n)
   (if (== $n 0)
       (quote (prepared-specialized-quote $f 1))
       ($f $n)))
!(prepared-specialized-quote (+ 1) 0)
!(prepared-specialized-quote (+ 1) 2)

; Negative boundary: an ordinary constructor is not opaque; active children
; retain PeTTa's eager argument semantics.
(= (prepared-specialized-box $f $x) (Box ($f $x)))
!(prepared-specialized-box (+ 1) 41)
