; A function's declarations govern every activation of it, whatever route
; reached it: a call starts with its callee, so binding a callable to a
; variable, passing it, storing it in a space or capturing it never changes
; a call's answers, their number, an argument's demand or the effects of
; evaluating it (approved difference 14, swi_differences.metta).  Each line
; compares a route with the written call it stands for and prints `same`.
;
; SWI-PeTTa applies a declaration only where its translator sees the
; callee.  The pinned 1.0.6 reference checks direct calls, eval, calls
; through head patterns and calls its specializer rewrites; through let,
; match, case, a retrieved parameter, a closure or a partial's completion
; it accepts wrong arguments and results, evaluates Atom arguments, and
; runs one declaration where there are two.

(: cr-id (-> Number Number))
(= (cr-id $x) $x)
(: cr-bad (-> Number Number))
(= (cr-bad $x) not-a-number)
(: cr-raw (-> Atom %Undefined%))
(= (cr-raw $x) (Box $x))
(: cr-two (-> Number Atom))
(: cr-two (-> Atom Atom))
(= (cr-two $x) (Seen $x))
(: cr-pair (-> Atom Atom %Undefined%))
(= (cr-pair $x $y) (Pair $x $y))
(: cr-sum (-> Number Number %Undefined%))
(= (cr-sum $x $y) (Sum $x $y))
(CrHeld id cr-id)
(CrHeld bad cr-bad)
(CrHeld raw cr-raw)
(CrHeld two cr-two)
(CrHeld sum cr-sum)
(= (cr-fetch $key) (match &self (CrHeld $key $f) $f))
(= (cr-wrong) wrong)
(= (cr-apply $f $x) ($f $x))
(= (cr-apply-plus $f $x) ($f (+ $x 2)))
(= (cr-apply-call (Call $f $x)) ($f $x))
(= (cr-same $route $written $via)
   (let $w (msort $written)
     (let $v (msort $via)
       (if (== $w $v) (Route $route same $w) (Route $route differs $w $v)))))

; Arguments and results are checked on every route.
!(cr-same via-let (collapse (cr-id (cr-wrong)))
          (collapse (let $f cr-id ($f (cr-wrong)))))
!(cr-same via-let-star (collapse (cr-id (cr-wrong)))
          (collapse (let* (($f cr-id) ($y (cr-wrong))) ($f $y))))
!(cr-same via-case (collapse (cr-id (cr-wrong)))
          (collapse (case (cr-fetch id) (($f ($f (cr-wrong)))))))
!(cr-same via-space (collapse (cr-id (cr-wrong)))
          (collapse (let $f (cr-fetch id) ($f (cr-wrong)))))
!(cr-same via-space-result (collapse (cr-bad 1))
          (collapse (let $f (cr-fetch bad) ($f 1))))
!(cr-same via-parameter (collapse (cr-id (cr-wrong)))
          (collapse (cr-apply cr-id (cr-wrong))))
!(cr-same via-retrieved-parameter (collapse (cr-bad 1))
          (collapse (let $f (cr-fetch bad) (cr-apply $f 1))))
!(cr-same via-head-pattern (collapse (cr-id wrong))
          (collapse (cr-apply-call (Call cr-id wrong))))
!(cr-same via-partial (collapse (cr-sum 1 (cr-wrong)))
          (collapse (let $p (cr-sum 1) ($p (cr-wrong)))))
!(cr-same via-retrieved-partial (collapse (cr-sum 1 (cr-wrong)))
          (collapse (let $f (cr-fetch sum) (let $p ($f 1) ($p (cr-wrong))))))
!(cr-same via-eval (collapse (cr-id wrong))
          (collapse (let $e (cons-atom cr-id (wrong)) (eval $e))))
!(cr-same via-closure (collapse (cr-bad 1))
          (collapse (let $f cr-bad (let $g (|-> ($x) ($f $x)) ($g 1)))))
!(cr-same via-superpose (collapse (superpose ((cr-id 1) (cr-bad 1))))
          (collapse (let $f (superpose (cr-id cr-bad)) ($f 1))))
!(cr-same via-map-atom (collapse (map-atom (1 wrong) $x (cr-id $x)))
          (collapse (let $f (cr-fetch id) (map-atom (1 wrong) $x ($f $x)))))

; An Atom argument reaches the callee as written on every route.
!(cr-same via-let-raw (collapse (cr-raw (+ 1 2)))
          (collapse (let $f cr-raw ($f (+ 1 2)))))
!(cr-same via-space-raw (collapse (cr-raw (+ 1 2)))
          (collapse (let $f (cr-fetch raw) ($f (+ 1 2)))))
!(cr-same via-parameter-raw (collapse (cr-raw (+ 1 2)))
          (collapse (cr-apply-plus cr-raw 1)))
!(cr-same via-retrieved-parameter-raw (collapse (cr-raw (+ 1 2)))
          (collapse (let $f (cr-fetch raw) (cr-apply-plus $f 1))))
!(cr-same via-lambda-raw (collapse (cr-raw (+ 1 2)))
          (collapse (let $g (|-> ($f $x) ($f (+ $x 2))) ($g cr-raw 1))))
!(cr-same via-closure-raw (collapse (cr-raw (+ 1 2)))
          (collapse (let $f cr-raw (let $g (|-> ($x) ($f (+ $x 2))) ($g 1)))))
!(cr-same via-partial-raw (collapse (cr-pair (+ 1 2) (+ 3 4)))
          (collapse (let $p (cr-pair (+ 1 2)) ($p (+ 3 4)))))

; Inside an equation body, too.
(= (cr-body-let $x) (let $f cr-raw ($f (+ $x 2))))
(= (cr-body-space $x) (let $f (cr-fetch raw) ($f (+ $x 2))))
(= (cr-body-partial $x) (let $p (cr-pair (+ $x 2)) ($p (+ $x 3))))
!(cr-same via-body-let (collapse (cr-raw (+ 1 2))) (collapse (cr-body-let 1)))
!(cr-same via-body-space (collapse (cr-raw (+ 1 2))) (collapse (cr-body-space 1)))
!(cr-same via-body-partial (collapse (cr-pair (+ 1 2) (+ 1 3)))
          (collapse (cr-body-partial 1)))

; Each declaration is an alternative that evaluates its own arguments: the
; Number one evaluates this argument, the Atom one holds it.
!(cr-same via-chains (collapse (cr-two (+ 1 2)))
          (collapse (let $f (cr-fetch two) ($f (+ 1 2)))))
!(cr-same via-chains-nondeterministic (collapse (cr-two (superpose (1 2))))
          (collapse (let $f cr-two ($f (superpose (1 2))))))
(= (cr-body-chains) (let $f cr-two ($f (superpose (1 2)))))
!(cr-same via-body-chains (collapse (cr-two (superpose (1 2))))
          (collapse (cr-body-chains)))
!(cr-same via-lambda-chains (collapse (cr-two (+ 1 2)))
          (collapse (let $g (|-> ($f) ($f (+ 1 2))) ($g cr-two))))

; A head that is itself an expression runs with its arguments, as one
; tuple, before the callee is known; the callee's checks still apply.
!(collapse ((cr-fetch raw) (+ 1 2)))
!(collapse ((cr-fetch id) (cr-wrong)))
