;; A higher-order function specializes on a function or a partial
;; application it receives.  An expression that only looks like an
;; under-application, because it is a value, is data: the call keeps it.

(= (add $a $b) (+ $a $b))
(= (wrap $x) (w $x))
(= (app $f $x) ($f $x))
(= (outer $g $d) (inner $g $d))
(= (inner $h $e) ($h $e))

(stored (add 1))

;; Source under-applications are partial applications.
!(app (add 1) 5)
!(outer (add 1) 5)

;; A stored expression is data wherever it flows.
!(let $e (match &self (stored $x) $x) (app $e 5))
!(match &self (stored $x) (app $x 5))
!(let $e (match &self (stored $x) $x) (outer wrap $e))
!(let $e (match &self (stored $x) $x) (outer $e 5))
