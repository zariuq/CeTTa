; Raw and quoted arguments.  A position whose declared type is `(Data A)`
; holds its argument as written; a quotation `@e` is a sealed code value.
; Neither is computed where it is passed.  Running held syntax (`eval`) or
; activating code (`*`) computes it afresh each time, so two runs are two
; independent choices, while a parameter is one shared choice.  `Atom` and
; `Expression` positions carry no evaluation control: their arguments are
; computed like any other.  Predicted by the call-time-choice core's quotation
; and activation rules: sameHeldPick_need (held code makes no choice),
; twicePick_need (each activation chooses afresh), actParam_need, and
; samePick_need for the computed positions.

(= (pick) A)
(= (pick) B)

; 1. A Data position holds its argument: one answer, nothing chosen.
(: hold2 (-> Atom Atom))
(= (hold2 $x) (Pair $x $x))
!(hold2 (pick))

; 2. Each explicit run of held syntax chooses afresh: four answers.
(: run2 (-> Atom Proc))
(= (run2 $x) (Pair (eval $x) (eval $x)))
!(run2 (pick))

; 3. Passing a quotation does not run it; each activation runs it afresh.
(= (konst $x $y) $x)
!(konst A @(pick))
(= (twice $c) (Pair *$c *$c))
!(twice @(pick))

; 4. Activating a parameter once runs the code as a direct activation does.
(= (act $c) *$c)
!(act @(S (pick)))
!(unquote @(S (pick)))

; 5. Atom and Expression positions are computed, and shared.
(: atomPair (-> Proc Proc))
(= (atomPair $x) (Pair $x $x))
!(atomPair (pick))
(: exprPair (-> Expression Proc))
(= (exprPair $x) (Pair $x $x))
!(exprPair (pick))
