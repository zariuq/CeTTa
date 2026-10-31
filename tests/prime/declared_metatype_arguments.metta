; A call declared to answer an atom of a metatype is accepted at a parameter
; of that metatype.  A call declared with another type is refused.
(: metatype:symbol (-> Symbol))
(= (metatype:symbol) baz)
(: metatype:expression (-> Expression))
(= (metatype:expression) (a b))
(: metatype:grounded (-> Grounded))
(= (metatype:grounded) 5)
(: metatype:variable (-> Variable))
(= (metatype:variable) $v)
(: metatype:number (-> Number))
(= (metatype:number) 5)
(: metatype:takes-symbol (-> Symbol Atom))
(= (metatype:takes-symbol $x) $x)
(: metatype:takes-grounded (-> Grounded Atom))
(= (metatype:takes-grounded $x) $x)
(: metatype:takes-variable (-> Variable Atom))
(= (metatype:takes-variable $x) $x)

!(metatype:takes-symbol (metatype:symbol))
!(metatype:takes-grounded (metatype:grounded))
!(let $r (metatype:takes-variable (metatype:variable)) (get-metatype $r))

; Written values are judged by their form.
!(metatype:takes-symbol baz)
!(metatype:takes-grounded 5)
!(metatype:takes-symbol 5)

; Calls declared with another metatype or another type.
!(metatype:takes-symbol (metatype:expression))
!(metatype:takes-grounded (metatype:symbol))
!(metatype:takes-variable (metatype:grounded))
!(metatype:takes-symbol (metatype:number))
