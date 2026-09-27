; Why it matters: specializing an interpreter to one known program turns a
; flexible description into fast straight-line code (the first Futamura
; projection), with no code generator outside the language.
;
; A tiny expression language over one input X has two meanings:
; - `interp` walks the syntax tree on every call;
; - `pe` walks it once, with a binding-time split: a Static part is computed
;   now, a Dynamic part becomes code.  `compile` closes the residual code
;   under `lam x`, and activating it with `*` gives an ordinary lambda.
; The generated code is a sealed value: it can be printed, read back from
; text, and run, and both routes agree on every input tried.

; ---- the interpreter
(= (interp (Lit $n) $x) $n)
(= (interp X $x) $x)
(= (interp (Add $a $b) $x) (+ (interp $a $x) (interp $b $x)))
(= (interp (Mul $a $b) $x) (* (interp $a $x) (interp $b $x)))
(= (interp (Pow $a $n) $x)
   (if (== $n 0) 1 (* (interp $a $x) (interp (Pow $a (- $n 1)) $x))))

; ---- the partial evaluator: (Static number) or (Dynamic code)
(= (code-of (Static $n)) @$n)
(= (code-of (Dynamic $c)) $c)

(= (add-bt $l $r)
   (unify $l (Static $m)
     (unify $r (Static $n)
       (let $s (+ $m $n) (Static $s))
       (if (== $m 0) $r (dyn-add $l $r)))
     (unify $r (Static $n)
       (if (== $n 0) $l (dyn-add $l $r))
       (dyn-add $l $r))))
(= (dyn-add $l $r)
   (let (quote $a) (code-of $l)
     (let (quote $b) (code-of $r) (Dynamic @(+ $a $b)))))

(= (mul-bt $l $r)
   (unify $l (Static $m)
     (unify $r (Static $n)
       (let $p (* $m $n) (Static $p))
       (if (== $m 0) (Static 0) (if (== $m 1) $r (dyn-mul $l $r))))
     (unify $r (Static $n)
       (if (== $n 0) (Static 0) (if (== $n 1) $l (dyn-mul $l $r)))
       (dyn-mul $l $r))))
(= (dyn-mul $l $r)
   (let (quote $a) (code-of $l)
     (let (quote $b) (code-of $r) (Dynamic @(* $a $b)))))

(= (pow-bt $b $n)
   (if (== $n 0) (Static 1)
       (if (== $n 1) $b (mul-bt $b (pow-bt $b (- $n 1))))))

(= (pe (Lit $n)) (Static $n))
(= (pe X) (Dynamic @x))
(= (pe (Add $a $b)) (add-bt (pe $a) (pe $b)))
(= (pe (Mul $a $b)) (mul-bt (pe $a) (pe $b)))
(= (pe (Pow $a $n)) (pow-bt (pe $a) $n))

(= (compile $program)
   (let (quote $body) (code-of (pe $program)) @(lam x $body)))

; ---- a program: 3*X^3 + (2*5 + X*0)
(= (poly) (Add (Mul (Lit 3) (Pow X 3)) (Add (Mul (Lit 2) (Lit 5)) (Mul X (Lit 0)))))

; The residual code: the power is unrolled, 2*5 is folded, X*0 is gone.
!(compile (poly))
; Activation turns the code into a function; both routes agree.
!(let $code (compile (poly))
   (let $f *$code
     (Agree
       (== ($f 0) (interp (poly) 0))
       (== ($f 1) (interp (poly) 1))
       (== ($f 2) (interp (poly) 2))
       (== ($f 5) (interp (poly) 5)))))
!(let $code (compile (poly)) (*$code 2))
!(interp (poly) 2)

; A program with no dynamic part compiles to a constant function.
!(compile (Add (Pow (Lit 2) 5) (Lit 10)))

; The code survives a round trip through text, and still runs.
!(let $code (compile (poly)) (repr $code))
!(let $code (compile (poly))
   (let $text (repr $code)
     (let $back (parse $text)
       (Reread (noreduce-eq $back $code) (*$back 2)))))
