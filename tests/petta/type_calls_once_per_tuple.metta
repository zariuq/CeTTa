; Typed calls as SWI-PeTTa stable-fixed runs them (last aligned at 9e8fbb6).
; Every answer below is stable-fixed's, except where a line says that a call
; through a variable keeps the declarations (approved difference 14): there
; it is the written call's answer.

; An application's kind comes from the function's equations; a declaration
; adds no arity.  A unary declaration types the unary application of a
; binary function, which stays a partial application.
(: tc-short (-> Number %Undefined%))
(= (tc-short $x $y) (Two $x $y))
!(collapse (tc-short 1))
!(collapse ((tc-short 1) 2))
!(collapse (tc-short "one"))
(: tc-nullary (-> %Undefined% %Undefined%))
(= (tc-nullary $x) (One $x))
!(collapse (tc-nullary))
; A declaration longer than every equation does not make a call of the
; over-application, which raises before its arguments run.
(: tc-long (-> Atom Atom %Undefined%))
(= (tc-long $x) (One $x))
!(catch (tc-long a b))
!(catch (tc-long a (println! SHOULD-NOT-RUN)))

; A complete call is typed by the declarations of its own number of inputs.
(: tc-md (-> Number Number))
(: tc-md (-> Number Number Bool))
(= (tc-md $x) $x)
(= (tc-md $x $y) True)
!(collapse (tc-md 1))
(: tc-touch (-> Atom %Undefined%))
(: tc-touch (-> Atom Atom %Undefined%))
(= (tc-touch $x) (let $_ (add-atom &self tc-hit) (empty)))
(= (tc-touch $x $y) unused)
!(collapse (once (superpose ((tc-touch z) fallback))))
!(collapse (match &self tc-hit tc-hit))

; A typed call runs its body once for each tuple of argument values.  The
; declarations decide whether it runs and which results it keeps.
(: tc-two (-> %Undefined% Atom %Undefined%))
(: tc-two (-> %Undefined% Atom Atom))
(= (tc-two $a $b) (Pair $a $b))
(= (tc-stamp $m) (let $_ (add-atom &self (TcSeen $m)) 1))
!(collapse (tc-two (tc-stamp direct) z))
!(collapse ((tc-two (tc-stamp staged)) z))
!(collapse (let $p (tc-two (tc-stamp bound)) ($p z)))
!(collapse (match &self (TcSeen $m) $m))
; An argument is held only when every declaration holds it.
(: tc-either (-> Atom %Undefined%))
(: tc-either (-> Number %Undefined%))
(= (tc-either $x) (called $x))
!(collapse (tc-either 7))
!(collapse (tc-either (+ 1 2)))
!(collapse (tc-either (quote (+ 1 2))))
; A value with two types under one declaration with a type variable.
(: tc-two-typed A)
(: tc-two-typed B)
(: tc-one-typed B)
(: tc-poly (-> $a Number))
(= (tc-poly $x) 1)
!(collapse (tc-poly tc-two-typed))
(: tc-same-type (-> $a $a Number))
(= (tc-same-type $x $y) 2)
!(collapse (tc-same-type tc-two-typed tc-one-typed))
!(collapse (tc-same-type tc-one-typed 5))
; Overloads still select by type, and each result is kept once.
(: tc-pick (-> Number Number))
(: tc-pick (-> String String))
(= (tc-pick $x) $x)
!(collapse (tc-pick (superpose (1 "a" True))))
(: tc-both (-> Number Number))
(: tc-both (-> Number String))
(= (tc-both $x) $x)
(= (tc-both $x) "s")
!(collapse (tc-both 1))
; An argument that every declaration rejects stops the later ones.
(: tc-early (-> Number Number %Undefined%))
(: tc-early (-> String Number %Undefined%))
(= (tc-early $x $y) ok)
!(collapse (tc-early True (add-atom &self tc-boom)))
!(collapse (match &self tc-boom found))
; Declarations identical up to renaming, and declarations that are not.
(: tc-alpha (-> $a $a))
(: tc-alpha (-> $b $b))
(= (tc-alpha $x) $x)
!(collapse (tc-alpha 7))
(: tc-variants (-> $a $a Atom))
(: tc-variants (-> $b $c Atom))
(= (tc-variants $x $y) ok)
!(collapse (tc-variants 1 1))
!(collapse (tc-variants 1 "one"))

; A partial application is typed by the declarations of at least as many
; inputs, once, and checks no result.
(: tc-pc (-> Atom Atom %Undefined%))
(: tc-pc (-> Atom Atom Atom %Undefined%))
(= (tc-pc $x $y) (Two $x $y))
(= (tc-pc $x $y $z) (Three $x $y $z))
!(collapse (tc-pc x))
!(collapse (tc-pc x (+ 1 2)))
!(collapse ((tc-pc x) (+ 1 2)))
; Through a variable the declarations hold (+ 1 2) as the written call does
; (approved difference 14).
!(collapse (let $p (tc-pc x) ($p (+ 1 2))))
(: tc-result (-> Number Number String))
(= (tc-result $a $b) (+ $a $b))
!(collapse (tc-result 1))
!(collapse ((tc-result 1) 2))
!(collapse (let $p (tc-result 1) ($p 2)))

; A written partial head continues its partial application: the new
; arguments take the declarations' demand at their own positions.
(: tc-held (-> Number Atom %Undefined%))
(= (tc-held $a $b) (Pair $a $b))
!(collapse (tc-held 1 (+ 1 1)))
!(collapse ((tc-held 1) (+ 1 1)))
!(collapse ((tc-held 1) (/ 1 0)))
(: tc-number (-> Atom Number %Undefined%))
(= (tc-number $a $b) (Pair $a $b))
!(collapse ((tc-number x) (+ 1 1)))
!(collapse ((tc-number x) wrong))
; The head's type evidence carries on: an argument of the completion that
; erases it does not undo the check the head already made.
(: tc-mark A)
(: tc-witness A)
(: tc-evidence (-> $t $t Number Number))
(= (tc-evidence $x $y $n) 9)
(= (tc-erase) (let $_ (remove-atom &self (: tc-mark A)) tc-witness))
!(collapse ((tc-evidence tc-mark (tc-erase)) 0))
!(add-atom &self (: tc-mark A))
!(collapse (((tc-evidence tc-mark) (tc-erase)) 0))
; The head's surviving declarations must reach the completion's: none does,
; so the completion's arguments never run.
(: tc-cross (-> Number String Number))
(: tc-cross (-> Bool Bool Bool Number))
(= (tc-cross $x $y) 1)
(= (tc-cross $x $y $z) 2)
!(collapse ((tc-cross 1) (add-atom &self tc-cross-boom) True))
!(collapse (match &self tc-cross-boom found))
; A head computed by a call runs with its arguments as one tuple.
(: tc-raw (-> Atom %Undefined%))
(= (tc-raw $x) (Box $x))
(= (tc-fetch $f) $f)
!(collapse ((tc-fetch tc-raw) (+ 1 2)))
!(collapse (tc-raw (+ 1 2)))

; The same calls inside an equation body.
(= (tc-in-body-two) (tc-two (tc-stamp body) z))
!(collapse (tc-in-body-two))
!(collapse (match &self (TcSeen body) body))
(= (tc-in-body-either) (tc-either (+ 1 2)))
!(collapse (tc-in-body-either))
(= (tc-in-body-poly) (tc-poly tc-two-typed))
!(collapse (tc-in-body-poly))
(= (tc-in-body-partial) (tc-pc x))
!(collapse (tc-in-body-partial))
(= (tc-in-body-staged) ((tc-held 1) (+ 1 1)))
!(collapse (tc-in-body-staged))
