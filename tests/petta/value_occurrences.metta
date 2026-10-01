; A variable denotes the value bound to it, and that value is never
; evaluated again, as in SWI-PeTTa, where a variable compiles to a Prolog
; variable.  Here `(foo)` spells a call, so evaluating the value again would
; turn it into 42.
(= (foo) 42)
(= (wrapit $x) ($x))
(= (g $x $y) ($x $y))
(= (first-of $l) (car-atom $l))
; lambda bodies read their parameters as values
!(maplist (|-> ($x) ($x)) (quote ((foo) 1)))
!((|-> ($x) (wrapit $x)) (quote (foo)))
!(let $v (quote (foo)) ((|-> ($y) ($y $y)) $v))
!(maplist (|-> ($x) (first-of $x)) (quote (((foo) 2) (3 4))))
; a lambda of two parameters is one application
!(let $f (|-> ($a $b) ($b $a)) ($f (quote (foo)) 1))
; a lambda without parameters runs its body
!(let $h (|-> () (foo)) ($h))
; filter-atom conditions read the element as a value
!(filter-atom (quote ((foo) 1)) $x (== ($x) ((foo))))
!(filter-atom (quote ((foo) 1)) $x (== $x (quote (foo))))
; a partial application holds values
!(let $p (g (quote (foo))) ($p 1))
; cons and == take their operands as written
!(let $v (quote (foo)) (let ($a $b) (cons $v (2)) $a))
!(let $v (quote (foo)) (== $v (foo)))
; a lexical fold body reads its element as a value
!(foldl-atom (quote ((foo) 1)) () $acc $x (let $z (cons $x $acc) $z))
; a fold step that is only a binder is the value it holds
!(foldl-atom (quote (1 (foo))) 0 $acc $x $x)
!(foldl-atom (quote (1)) (quote (foo)) $acc $x $acc)
!(foldl-atom (quote ((foo))) 0 $acc $x (first-of ($x)))
; the pair selectors answer the element as it is
!(let $p (quote ((foo) 1)) (first-from-pair $p))
!(let $p (quote (1 (foo))) (second-from-pair $p))
; get-type types the value, not what it would compute
!(let $v (quote (foo)) (get-type $v))
