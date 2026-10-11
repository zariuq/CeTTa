; Reflection: `parse` reads text into syntax, `repr` prints syntax back, and
; the `syn:` forms are the expanded spellings of the compact reader forms.
;
;   $@K  = (syn:var (quote K))    &@K = (syn:ref (quote K))
;   @K   = (quote K)              *@K = (unquote (quote K))
;   !(e) = (syn:exec e)
;
; parse(repr(S)) is alpha-equivalent to S.  `syn:syntactic-equal` is
; identity-sensitive; `=alpha` identifies syntax up to variable renaming.
; At the end the program reads its own source text, parses its own one-line
; equations, and checks them against the equations it is running.

!(import! &self syn)
!(import! &self fs)

; 1-5. Compact and expanded spellings read to the same syntax.
!(=alpha (parse "$@(hole 0)") (parse "(syn:var (quote (hole 0)))"))
!(== (parse "&@(agent ada)") (parse "(syn:ref (quote (agent ada)))"))
!(== (parse "@(agent ada)") (parse "(quote (agent ada))"))
!(== (parse "*@(agent ada)") (parse "(unquote (quote (agent ada)))"))
!(== (parse "!(+ 1 2)") (parse "(syn:exec (+ 1 2))"))

; 6-8. repr prints the compact spelling, and parse inverts it.
!(repr (quote (agent ada)))
!(let $form (parse "!(+ 1 2)") (repr $form))
!(let $t (parse "(rule $@(hole 0) (pair $@(hole 0) $x))")
   (=alpha (parse (repr $t)) $t))

; 9-12. Identity-sensitive equality against alpha-equivalence.
!(syn:syntactic-equal (f $x $x) (f $y $y))
!(=alpha (f $x $x) (f $y $y))
!(=alpha (f $x $y) (f $y $y))
!(let $left (parse "(pair $x $x)")
   (let $right (parse "(pair $x $x)")
     (pair (syn:syntactic-equal $left $right) (=alpha $left $right))))

; 13-14. Reading keeps variable sharing within one read.
!(=alpha (parse "(pair $@(hole 0) $@(hole 0))") (pair $x $x))
!(=alpha (parse "(pair $@(hole 0) $@(hole 1))") (pair $x $x))

; 15-16. A parse failure is an Error; `try` turns it into a verdict.
;        (A bare Error at top level would end the run.)
!(try (parse "(A B"))
!(try (parse "(syn:var (compound x))"))

; 17-18. Text to code and back: parse a program text, lift it to code, run
;        it as code.
!(let $c (lift (parse "(+ 20 22)")) (pair $c *$c))
!(let $t (parse "(pair $a $a)") (repr $t))

; --- a program that reads its own source ----------------------------------------
(= (double $x) (* 2 $x))
(= (square $x) (* $x $x))
(= (compose $f $g $x) ($f ($g $x)))

(= (source-path) "examples/prime/metaprogramming/inv_05_reflection.metta")

; Parse each line; keep each line that reads as one equation, as a code
; value.  A line that is not one form (a comment, a fragment of a longer
; form) is reported Failed by `try`.  The verdict is lifted to code and taken
; apart as code, so the equation is kept as written: a bare parsed equation
; in an evaluated position would have its left side evaluated (see query 22).
(= (own-equations $lines)
   (if (== $lines ())
       ()
       (let ($line $rest) (decons-atom $lines)
         (let $more (own-equations $rest)
           (let $read (lift (try (parse $line)))
             (unify $read (quote (Established ((= $lhs $rhs))))
               (cons-atom (quote (= $lhs $rhs)) $more)
               $more)){$read}){$more}){$line $rest}))

; The head symbol and arity of an equation, read from its code.  (The result
; type is a variable: a declared result type Atom would hold the body, see
; inv_04 gap 2.)
(: form-signature (-> (Data Atom) $r))
(= (form-signature (= $lhs $rhs)) (Defines (car-atom $lhs) (- (size-atom $lhs) 1)))
(= (signature (quote $form)) (form-signature $form))

; Whether the running program holds an equation that unifies with the form.
; The code is opened with `unify`, so its payload is used as written.
(= (loaded $code)
   (unify $code (quote $eq)
     (if (== (collapse (match &self $eq found){$eq}) ()) missing loaded)
     not-code))

; 19. The one-line equations of this file, read from its own text.
!(let $eqs (own-equations (fs:read-lines (source-path)))
   (map-atom $eqs $e (signature $e)))
; 20. Each parsed equation is one the running program holds: the source text
;     and the loaded code agree.
!(let $eqs (own-equations (fs:read-lines (source-path)))
   (map-atom $eqs $e (loaded $e)))
; 21. A parsed equation, instantiated at chosen arguments and run.
!(let $eqs (own-equations (fs:read-lines (source-path)))
   (let $compose-code (index-atom $eqs 2)
     (unify $compose-code (quote (= (compose double square 3) $body))
       (Ran (quote $body) (eval $body))
       none)))
; 22. Contrast: the same parsed equation placed bare in a constructor field is
;     evaluated there, so its left side (double $x) becomes (* 2 $x).
!(let $f (parse "(= (double $x) (* 2 $x))") (Bare $f))
!(let $f (lift (parse "(= (double $x) (* 2 $x))")) (Quoted $f))
