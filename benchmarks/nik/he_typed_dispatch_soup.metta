; Closed overloads exercise the direct HE consistency prefilter.  The final
; Number branch is the only applicable declaration for the ground argument.

(: typed-choice (-> A00 Number))
(: typed-choice (-> A01 Number))
(: typed-choice (-> A02 Number))
(: typed-choice (-> A03 Number))
(: typed-choice (-> A04 Number))
(: typed-choice (-> A05 Number))
(: typed-choice (-> A06 Number))
(: typed-choice (-> A07 Number))
(: typed-choice (-> A08 Number))
(: typed-choice (-> A09 Number))
(: typed-choice (-> A10 Number))
(: typed-choice (-> A11 Number))
(: typed-choice (-> A12 Number))
(: typed-choice (-> A13 Number))
(: typed-choice (-> A14 Number))
(: typed-choice (-> A15 Number))
(: typed-choice (-> Number Number))

(= (typed-choice $value) $value)

(= (typed-dispatch-loop $n)
   (if (== $n 0)
       Done
       (let $value (typed-choice 7)
         (typed-dispatch-loop (- $n 1)))))

!(typed-dispatch-loop 20000)
