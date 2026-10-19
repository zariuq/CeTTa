; What evaluating a definition may do is read from all it evaluates: a write
; (world-write), a query (relational-query), or both.  A constructor's
; elements are evaluated and unquote runs the syntax it is given, so their
; effects count.  A symbol is not a call, even where an equation names it.
!(bind! &s (new-space))
!(add-atom &s (a 1))
(= (g) (match &s (a $x) $x))
(= foo (match &s (a $x) $x))
(= (writes) (add-atom &s (b 1)))
(= (writes-inside) (pair (add-atom &s (b 2)) done))
(= (queries-inside) (pair (g) done))
(= (names-a-query) foo)
(= (unquotes-a-write) (unquote (quote (add-atom &s (b 3)))))
(= (does-both) (pair (g) (add-atom &s (b 4))))
(= (pure $x) (pair (+ $x 1) done))
; A case's or a switch's branches are (pattern value) pairs: the patterns are
; matched, never evaluated (a variable pattern is no call), and the values'
; calls count.
(= (cases $x) (case $x ((a 1) ($r 2))))
(= (case-writes $x) (case $x ((a (add-atom &s (c 1))) ($r 2))))
(= (case-queries $x) (case (g) ((1 yes) ($r no))))
(= (switch-writes $x) (switch $x ((a 1) ($r (add-atom &s (c 2))))))
!(writes)
!(writes-inside)
!(queries-inside)
!(names-a-query)
!(unquotes-a-write)
!(does-both)
!(pure 1)
!(cases b)
!(case-writes a)
!(case-queries x)
!(switch-writes b)
!(collapse (match &s (b $n) $n))
