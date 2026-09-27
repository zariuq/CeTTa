; Why it matters: code that arrives from elsewhere (a planner, a learner, a
; model, a user) can be examined and improved as syntax before anything runs,
; and refused when it reaches an effect it has no permission for.
;
; Every parameter here is Data, so the submitted code is held as written.
; `simp` rewrites it bottom-up by equations (x+0, x*1, constant folding) and
; never enters a sealed quotation.  `effects-in` looks for effectful
; operations in the code and, following calls, in the program's own equations
; (read as sealed code, never run).  `run-checked` runs only code with no
; effects, after simplification, by explicit evaluation.

; ---- rewriting held code
(: literal? (-> (Data Atom) Bool))
(= (literal? $x)
   (if (== (get-metatype $x) Grounded)
       (let $v (eval $x) (== (get-type $v) Number))
       False))
(: simp (-> (Data Atom) (Data Atom)))
(= (simp $e)
   (if (== (get-metatype $e) Expression)
       (if (noreduce-eq $e ()) ()
           (let $h (car-atom $e)
             (if (noreduce-eq $h quote) $e
                 (let $kids (simp-list $e) (simp-node $kids)))))
       $e))
(: simp-list (-> (Data Atom) (Data Atom)))
(= (simp-list $e)
   (if (noreduce-eq $e ()) ()
       (let ($h $t) (decons-atom $e)
         (let $h2 (simp $h)
           (let $t2 (simp-list $t)
             (cons-atom $h2 $t2))))))
(: simp-node (-> (Data Atom) (Data Atom)))
(= (simp-node $e)
   (unify $e (+ 0 $x) $x
   (unify $e (+ $x 0) $x
   (unify $e (* 1 $x) $x
   (unify $e (* $x 1) $x
   (unify $e (+ $a $b)
     (if (and (literal? $a) (literal? $b)) (let $s (+ (eval $a) (eval $b)) $s) $e)
   (unify $e (* $a $b)
     (if (and (literal? $a) (literal? $b)) (let $p (* (eval $a) (eval $b)) $p) $e)
   $e)))))))

; ---- inspecting held code, following calls into the program's equations
(: effect? (-> (Data Atom) Bool))
(= (effect? $h)
   (if (== (get-metatype $h) Symbol)
       (case (repr $h)
         (("println!" True) ("add-atom" True) ("remove-atom" True)
          ("bind!" True) ("import!" True) ("new-space" True) ($_ False)))
       False))
(: heads (-> (Data Atom) (Data Atom)))
(= (heads $e)
   (if (== (get-metatype $e) Expression)
       (if (noreduce-eq $e ()) ()
           (let ($h $t) (decons-atom $e)
             (let $a (heads $h)
               (let $b (heads $t)
                 (let $ab (union-atom $a $b)
                   (if (== (get-metatype $h) Symbol) (union-atom ($h) $ab) $ab))))))
       ()))
(: elements (-> (Data Atom) (Data Atom)))
(= (elements $l)
   (if (noreduce-eq $l ()) (empty)
       (let ($h $t) (decons-atom $l) (superpose ($h (elements $t))))))
; The right-hand sides of a symbol's equations, each as sealed code.
(= (bodies-of $h)
   (collapse (match &self (= $lhs $rhs) (if (== (car-atom $lhs) $h) @$rhs (empty)))))
(: effect-in (-> (Data Atom) Number (Data Atom)))
(= (effect-in $e $depth)
   (let $hs (heads $e)
     (let $h (elements $hs)
       (if (effect? $h) $h
           (if (> $depth 0)
               (let $bodies (bodies-of $h)
                 (let $q (elements $bodies)
                   (unify $q (quote $b) (effect-in $b (- $depth 1)) (empty))))
               (empty))))))
(: effects-in (-> (Data Atom) (Data Atom)))
(= (effects-in $e) (let $found (collapse (effect-in $e 3)) (unique-atom $found)))

; ---- the gate
(: run-checked (-> (Data Atom) $r))
(= (run-checked $e)
   (let $bad (effects-in $e)
     (if (== $bad ())
         (let $s (simp $e) (let $v (eval $s) (Ran $s $v)))
         (Refused $bad))))

; ---- a small program the submitted code may call
(= (inc $x) (+ $x 1))
(= (loud $x) (let $u (println! "loud ran") $x))
(= (report $x) (inc (loud $x)))
(= (spin $x) (spin $x))

; Rewriting: identities and folding; open code with a free y is fine; a
; sealed quotation is left exactly as written.
!(simp (+ (* 1 (+ 2 3)) 0))
!(simp (* y (+ 0 (* 2 3))))
!(simp (f @(+ 0 y)))
; A user call is kept as syntax while rewriting: (inc 0) runs only at the end.
!(run-checked (+ (* 1 (+ 2 3)) (inc 0)))
; Effects are found in the code itself, and behind a call two levels down.
!(effects-in (+ 1 (println! "launch")))
!(effects-in (+ (report 1) 2))
!(effects-in (spin 1))
; Refused code never runs: nothing is printed and nothing is added.
!(run-checked (+ 1 (println! "launch")))
!(run-checked (+ (report 1) 2))
!(run-checked (* (+ 0 (add-atom &self (secret 42))) 1))
!(collapse (match &self (secret $x) $x))
