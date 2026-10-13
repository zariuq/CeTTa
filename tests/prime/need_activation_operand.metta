; Activation forces its operand to weak head normal form: `*` on an equation
; parameter that holds code runs that code, as on a let-bound, case-bound or
; lambda-bound one.
(= (act $code) *$code)
(= (act-let $code) (let $c $code *$c))
(= (act-case $code) (case $code (($c *$c))))
(= (maybe-run $flag $c) (if $flag *$c skipped))
!(A0 *@(+ 1 2))
!(A1 (act @(+ 1 2)))
!(A2 (act-let @(+ 1 2)))
!(A3 (act-case @(+ 1 2)))
!(A4 (maybe-run True @(+ 1 2)))
!(A5 ((lam c *c) @(+ 1 2)))
