; Every place where plain PeTTa on CeTTa deliberately answers differently from
; SWI-PeTTa.  Each entry states SWI-PeTTa's answer, the reason CeTTa differs,
; and who decided it when.  CeTTa's answers are pinned in the .expected file,
; so an entry that drifts is noticed; a difference missing from this file is
; found only by running both engines.  SWI-PeTTa's answers are those of the
; pinned in-house snapshot 9b016a39, re-verified 2026-09-29.  An entry
; leaves this file only when the two engines agree.

(= (twice $x) (* 2 $x))
(= (boom) (/ 1 0))
(= (alt-a) (/ 1 0))
(= (alt-a) 1)
(= (alt-a) 2)
(= (alt-b) 1)
(= (alt-b) (/ 1 0))
(= (alt-b) 2)
(= (invoke $f) ($f))
(= (app1 $f $a) ($f $a))
(= (safe $x) $x)

; 1. An error inside a run-time dispatch fails that path alone, whatever the
;    exploration order.  SWI-PeTTa's catch discards the alternatives after the
;    raising one, so its answer depends on the order: () here.
;    Decided: Zar, 2026-09-26 (goal.md §1.8, settled).
!(let $f alt-a (msort (collapse ($f))))
; SWI-PeTTa: (1)
!(let $f alt-b (msort (collapse ($f))))

; 2. A dispatch keeps its handler when its caller is specialized.  SWI-PeTTa's
;    specializer drops it, and the error reaches the catch:
;    (Error (evaluation_error zero_divisor) ...).
;    Decided: Zar, 2026-09-26 (goal.md §1.8, preservation).
!(catch (collapse (invoke boom)))

; 3. `let` and `let*` reached at run time are data, as `if`, `case` and `quote`
;    are in both engines, and running the data later runs the binding.
;    SWI-PeTTa registers them as functions without defining one, so it
;    returns a partial, which `eval` leaves as it is: (partial let ($_0 1 2)),
;    (partial let* ((($_0 1)) 2)), and the partial again under eval.  Only a
;    `let` equation taking more arguments, defined later, completes it:
;    (user a b c d) in the last query, where the data here stays data.
;    A program that defines `let` equations beforehand gets its function at
;    run time in both engines (runtime_program_functions.metta).
;    Decided: Zar, 2026-09-26 17:50 (goal.md §5).
!(let $f let ($f $y 1 2))
!(let $f let* ($f (($y 1)) 2))
!(let $f let (let $p ($f $y 1 2) (eval $p)))
!(let $f let (let $p ($f a b c) (progn (add-atom &self (= (let $a $b $c $d) (user $a $b $c $d))) ($p d))))

; 4. An import invalidates the specializations of its space, so a specialized
;    function sees equations imported after it.  SWI-PeTTa keeps the stale
;    specialization: (1) after the import.
;    Decided: Oruži, 2026-09-26, as the unspecialized meaning; confirmed by
;    Zar committing it (ad1e6a94).
!(msort (collapse (app1 safe 1)))
!(import! &self swi_differences_import)
!(msort (collapse (app1 safe 1)))

; 5. Data spelled with `cons` and `nil` meets an authored list pattern as the
;    list it spells: `(cons $h $t)` in a head or a `case` takes
;    `(cons a (cons b nil))` apart as a and (cons b nil).  SWI-PeTTa reads the
;    value as the three-element list (cons a (cons b nil)): its head is `cons`
;    and `nil` is no empty list, so the walks below answer nothing there, and
;    the case answers cons.  The parser pack's forests are built this way and
;    rely on CeTTa's reading.
;    Recorded: Oruži, 2026-09-27 (the reading predates a09d40ae; the tier
;    follows the machine).  The contract is Zar's to set (goal.md §5).
(spelled-item (cons a (cons b nil)))
(= (spelled) (match &self (spelled-item $l) $l))
(= (walk nil) done)
(= (walk (cons $h $t)) (walk $t))
(= (heads nil) ())
(= (heads (cons $h $t)) (cons-atom $h (heads $t)))
; SWI-PeTTa: no answer.
!(walk (spelled))
; SWI-PeTTa: no answer.
!(heads (spelled))
; SWI-PeTTa: cons
!(case (spelled) (((cons $h $t) $h) ($other none)))

; 6. Retired 2026-09-29: the pinned reference compares numbers by exact
;    value too, so (== 3 3.0) is true and (!= 3 3.0) false in both engines.
;    Recorded 2026-09-27 against revision ae66fa8e; retired by Oruži on
;    re-verifying every entry against the pinned snapshot.

; 7. `new-space` makes a fresh space.  SWI-PeTTa has no `new-space`, so
;    (new-space) stays data there and is an Expression.  The extra form costs
;    SWI-PeTTa programs nothing: a named space such as &work serves wherever
;    a fresh one would, and a use of `new-space` translates to one.
;    Decided: Zar, 2026-09-27 ("extras are ok").
; SWI-PeTTa: Expression
!(let $s (new-space) (get-metatype $s))

; 8. An `if` whose branch has goals and a variable for its value keeps that
;    variable to the branch.  SWI-PeTTa's translator unifies the `if`'s output
;    with the variable itself, so the other branch's value must equal it too:
;    `(pick 20)` below must return 1 there, and the query has no answer.
;    A branch with no goals keeps them apart in both engines.
;    Decided: Zar, 2026-09-27, as a bug of SWI-PeTTa's translator.
(= (seven) 7)
(= (pick $h) (if (> $h 10) (let $c (seven) $h) (pick 20)))
; SWI-PeTTa: ()
!(collapse (pick 1))

; 9. An unbound variable in an answer prints as `$V` and its number;
;    SWI-PeTTa prints `$_` and the same number.  Both number an answer's
;    variables from 0 in order of first occurrence, so the two spellings
;    correspond one to one; the PeTTa corpus reads SWI-PeTTa's answers
;    through that correspondence.
;    Recorded: Oruži, 2026-09-28.
; SWI-PeTTa: (f $_0 $_1 $_0)
!(let $a (f $x $y $x) $a)

; 10. A forward declaration does not provide an equation to execute before
;     that equation is installed. Preserve the unknown expression, then use
;     the equation once it is present. SWI-PeTTa 9b016a39 predeclares the
;     function but aborts the first query with Unknown procedure, exit 2.
;     Decided: Zar, 2026-09-28, as a reference bug under the rule that unknown
;     expressions remain uninterpreted. Normal forward references after the
;     definitions are installed agree in both engines.
(= (before-install) (installed-later 5))
!(before-install)
(= (installed-later $n) (+ $n 1))
!(before-install)
