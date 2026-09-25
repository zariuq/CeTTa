; List values in the PeTTa lane's extended profile: [x1 ... xn] and the list
; pattern [x1 ... xk | rest].  A PeTTa document holds expressions, so each
; list appears inside one.

; They read and print as written, nested, empty, with brackets in strings.
!(quote [a [b c] [] "s]t" (f g)])
!(quote [a | $t])
!(quote [a | [b c]])
; A comma is a symbol character; a bar alone marks the rest.
!(quote [a,b c])
!(quote [|- a|b |x])
; A word keeps its brackets outside a list.
!(quote -[det]->)

; A list is data: its elements are not evaluated, as an argument, a body or
; a bound value.
!(id [a (+ 1 2)])
(= (listed) [a (+ 1 2)])
!(listed)
!(let $x [a (+ 1 2)] $x)
!(get-metatype [a b])

; A list never equals an expression, [] is not (), multiplicity is kept.
!(== [f a] (f a))
!(== [] ())
!(== [a b] [a b])
!(== [a a] [a])

; Head and rest.
!(let [$h | $t] [1 2 3] ($h $t))
!(let [$h | $t] [1] $t)
!(collapse (let [$h | $t] [] $h))
!(let [$a $b | $r] [1 2 3 4] ($a $b $r))
; A repeated variable, consistent and inconsistent.
!(let [$x $x | $r] [1 1 2] $r)
!(collapse (let [$x $x | $r] [1 2 3] $r))
; An expression pattern does not open a list, nor a list an expression.
!(collapse (let ($f $x) [a b] $f))
!(collapse (let [$f $x] (a b) $f))
; Bindings inside nested lists.
!(let (g [[$a $b] | $rest]) (g [[1 2] [3]]) ($a $b $rest))

; Substitution into a rest builds one list; alpha-equality keeps sharing.
!(let $t [b c] (quote [a | $t]))
!(=alpha [$x $y] [$z $w])
!(=alpha [$x $x] [$z $w])

; The sequence primitives read a list's elements.
!(car-atom [a b])
!(cdr-atom [a b])
!(cons-atom x [a])
!(decons-atom [a b])
!(size-atom [a b c])
!(index-atom [a b c] 1)

; The native sequence forms read a list's elements and keep its kind.
!(map-atom [1 2] $x (+ $x 1))
!(filter-atom [1 2 3] $x (> $x 1))
!(foldl-atom [1 2 3] 0 $acc $x (+ $acc $x))
!(reverse [a b c])
!(last [a b c])
!(msort [c a b])
!(length [a b c])
!(is-member b [a b c])
!(collapse (superpose [a b]))

; Equations over lists dispatch on list patterns.
(= (len []) 0)
(= (len [$h | $t]) (+ 1 (len $t)))
!(len [a b c])
!(len [])
(= (swap [$a $b]) [$b $a])
!(swap [1 2])

; A list stored in a space is matched back, as a value and by patterns.
!(add-atom &self (fact [1 2]))
!(add-atom &self (fact (1 2)))
!(collapse (match &self (fact [$x | $r]) ($x $r)))
!(collapse (match &self (fact ($x $y)) $x))
