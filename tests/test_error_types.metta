; Error type coverage

; IncorrectNumberOfArguments
(: binary-fn (-> Number Number Number))
(= (binary-fn $x $y) (+ $x $y))
!(assertEqualToResult (binary-fn 1) ((Error (binary-fn 1) IncorrectNumberOfArguments)))

; BadArgType on typed function
(: typed-add (-> Number Number Number))
(= (typed-add $x $y) (+ $x $y))
!(assertEqualToResult (typed-add 1 "hello") ((Error (typed-add 1 "hello") (BadArgType 2 Number String))))
