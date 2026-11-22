; Overload argument and result checks are correlated.  A mismatch in one
; applicable branch prunes that branch without preventing another overload.

(: choose (-> Type1 Type1))
(: choose (-> Type2 Type2))
(= (choose $value)
   (if (== $value T1in) T1out T2out))

(: T1in Type1)
(: T1out Type1)
(: T2in Type2)
(: T2out Type2)

!(test (choose T2in) T2out)
