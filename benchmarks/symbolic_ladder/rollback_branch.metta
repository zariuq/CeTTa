; The first equation binds $x and then fails the match. That binding
; does not escape. The next equation still answers.

(= (bad) (let $x changed (let $_ (match &self (no-such $z) $z) $x)))
(= (bad) kept)
!(bad)
