; List values: [x1 ... xn] and the list pattern [x1 ... xk | rest].

; They read and print as written, nested, empty, with brackets in strings.
!(quote [a [b c] [] "s]t" (f g)])
!(quote [a | $t])
!(quote [a | [b c]])
; Elements are separated by layout; a comma is a symbol character, a bar
; alone marks the rest, and a word may start with a bar when more follows.
!(quote [a,b c])
!(quote [|- a|b |x])
!(assertEqual (size-atom [a,b]) 1)
; Outside a list the profiles differ on a bracket inside a word: the HE
; extended profile keeps -[det]-> one word, while in Prime brackets end
; every token, so a bracket touching a word opens a substitution
; (tests/prime/lift_brackets.metta).  The test-list-value-lanes gate checks
; both.

; A list is data: its elements are not evaluated.
! [a (+ 1 2)]
!(get-metatype [a b])
!(get-type [a b])

; A list never equals an expression, [] is not (), multiplicity is kept.
!(assertEqual (== [f a] (f a)) False)
!(assertEqual (== [] ()) False)
!(assertEqual (== [a b] [a b]) True)
!(assertEqual (== [a a] [a]) False)

; Head and rest.
!(assertEqual (let [$h | $t] [1 2 3] ($h $t)) (1 [2 3]))
!(assertEqual (let [$h | $t] [1] $t) [])
!(assertEqual (collapse (let [$h | $t] [] $h)) ())
!(assertEqual (let [$a $b | $r] [1 2 3 4] ($a $b $r)) (1 2 [3 4]))
; A repeated variable, consistent and inconsistent.
!(assertEqual (let [$x $x | $r] [1 1 2] $r) [2])
!(assertEqual (collapse (let [$x $x | $r] [1 2 3] $r)) ())
; An expression pattern does not open a list, nor a list an expression.
!(assertEqual (collapse (let ($f $x) [a b] $f)) ())
!(assertEqual (collapse (let [$f $x] (a b) $f)) ())
; Bindings inside nested lists.
!(assertEqual
  (let (g [[$a $b] | $rest]) (g [[1 2] [3]]) ($a $b $rest))
  (1 2 [[3]]))
; Two list patterns meet by their prefixes and rests.
!(assertEqual (unify [a | $t] [a b | $u] $t no) [b | $u])
!(assertEqual (unify [a | $t] [a b] $t no) [b])

; Substitution into a rest builds one list; alpha-equality keeps sharing.
; (A list is data, so it needs no quotation here; in Prime a quotation is
; sealed and substitution would not enter it.)
!(assertEqual (let $t [b c] [a | $t]) [a b c])
!(assertEqual (=alpha [$x $y] [$z $w]) True)
!(assertEqual (=alpha [$x $x] [$z $w]) False)

; The sequence primitives read a list's elements.
!(assertEqual (car-atom [a b]) a)
!(assertEqual (cdr-atom [a b]) [b])
!(assertEqual (cons-atom x [a]) [x a])
!(assertEqual (decons-atom [a b]) (a [b]))
!(assertEqual (size-atom [a b c]) 3)
!(assertEqual (index-atom [a b c] 1) b)
!(assertEqual (unique-atom [a b a]) [a b])
!(assertEqual (union-atom [a] [b]) [a b])
; ...also inside equation bodies, which run on prepared intrinsics.
(= (list-cons $h $t) (cons-atom $h $t))
(= (list-decons $l) (decons-atom $l))
(= (list-union $l $r) (union-atom $l $r))
!(assertEqual (list-cons x [a]) [x a])
!(assertEqual (list-decons [a b]) (a [b]))
!(assertEqual (list-union [a] [b c]) [a b c])

; A list's alternatives are its elements; the tag is never one of them.
!(assertEqual (collapse (superpose [a b])) (a b))
!(assertEqual (collapse (hyperpose [a b])) (a b))

; Equations over lists dispatch on list patterns.
(= (len []) 0)
(= (len [$h | $t]) (+ 1 (len $t)))
!(assertEqual (len [a b c]) 3)
!(assertEqual (len []) 0)
(= (swap [$a $b]) [$b $a])
!(assertEqual (swap [1 2]) [2 1])
; No equation takes three elements, so the call remains as it is.
!(assertEqual (swap [1 2 3]) (swap [1 2 3]))

; A list stored in a space is matched back, as a value and by patterns.
!(bind! &s (new-space))
!(add-atom &s (fact [1 2]))
!(add-atom &s (fact (1 2)))
!(add-atom &s (rule (len [$h | $t]) body))
!(assertEqual (collapse (match &s (fact [$x | $r]) ($x $r))) ((1 [2])))
!(assertEqual (collapse (match &s (fact ($x $y)) $x)) (1))
!(assertEqual (collapse (match &s (rule (len [a b c]) $b) $b)) (body))
!(assertEqual (collapse (match &s (rule (len []) $b) $b)) ())
