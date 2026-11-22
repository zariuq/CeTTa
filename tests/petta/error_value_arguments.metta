; An (Error ...) written as data, returned by a function or bound by a let is
; an ordinary value: a typed call judges it by its declared type, like any
; other argument, and the run goes on.  An error is a raise its producer
; made, never read back from the shape of a value.
(: num-id (-> Number Number))
(= (num-id $x) $x)
(: expr-id (-> Expression Expression))
(= (expr-id $x) (Kept $x))
(= (made) (Error made value))

; Admitted: Expression admits an Error value.
!(expr-id (Error written value))
!(expr-id (made))
!(let $e (Error bound value) (expr-id $e))

; Refused: Number does not admit it, so the call answers nothing.
!(collapse (catch (num-id (Error written value))))
!(collapse (catch (num-id (made))))
!(let $e (Error bound value) (collapse (catch (num-id $e))))
!(collapse (catch (num-id (superpose (1 (made) 3)))))
!(println! continued)
