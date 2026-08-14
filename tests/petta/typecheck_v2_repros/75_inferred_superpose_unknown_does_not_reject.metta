; Negative-information control: an inferred heterogeneous result widens to
; unknown knowledge rather than becoming a closed union contract.  In default
; mode that absence of knowledge must not reject a Number-only consumer; a
; runtime check remains responsible for values the checker cannot establish.

(= (mixed-result)
   (superpose (1 "text")))

(: require-number (-> Number Bool))
(= (require-number $value) True)

(: use-mixed-as-number (-> Bool))
(= (use-mixed-as-number)
   (require-number (mixed-result)))
