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

; 5. Retired 2026-11-04: data spelled `(cons h t)` is data in both engines.
;    `cons` is a constructor: code builds and splits list cells, a stored
;    `(cons a (cons b nil))` is a three-element expression whose head is
;    `cons`, and `nil` is a symbol, not the empty list.  Decided by Zar
;    (11/04: "do the swi one. It's a bit more homoiconic"); the answers are
;    gated in tier_spelled_cells on both routes.

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

; 9. Retired 2026-09-29: variable spelling is not observable, in answers or
;    inside strings (Zar), so CeTTa's $V0 and SWI-PeTTa's $_0 name the same
;    answer. Recorded 2026-09-28; retired by Oruži on that decision.

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

; 11. A binding that would make a term cyclic has no answer: unification
;     checks, as it binds a variable, that the variable does not occur in
;     its value.  SWI-PeTTa runs with occurs_check=false and builds the
;     rational tree, so both queries below answer there.  CeTTa reads a
;     cyclic term that arrives from Prolog as the term it is, but its own
;     bindings keep terms finite.
;     Recorded: Oruži, 2026-09-29 (the check at the bind dates from
;     2026-09-22).  The contract is Zar's to set.
; SWI-PeTTa: done
!(let $x (f $x) done)
; SWI-PeTTa: ok
!(let* (($_ (translatePredicate (= $c (f $x $c)))) ($x (g $c))) ok)

; 12. A `let` pattern headed by a variable meets its value as a pattern.
;     SWI-PeTTa's translator runs such a pattern as a run-time dispatch,
;     whose data answer may not be cyclic, and runs it before the let's
;     value when that value is computed but after it when the value is a
;     variable: the query below has no answer there, while the same pattern
;     over (index-atom $c 2), the same term, has one.  CeTTa answers both.
;     Recorded: Oruži, 2026-09-29.  The contract is Zar's to set.
; SWI-PeTTa: no answer.
!(let $_ (translatePredicate (= $c (a b $c))) (let ($x $y $z) $c (== $z $c)))

; 13. Writing a term that is not a finite tree raises
;     representation_error(cyclic_term).  SWI-PeTTa's writer never ends on
;     such a term and runs out of stack, so what its catch gives is a
;     resource_error whose context describes the host's stack.  Uncaught,
;     both stop the run with status 2 (tests/petta/rational_terms_text).
;     Recorded: Oruži, 2026-09-29.  The contract is Zar's to set.
; SWI-PeTTa: (Error (resource_error stack) (dict stack_overflow ...))
!(let $_ (translatePredicate (= $c (f $c))) (catch (repr $c)))

; 14. A call uses the declarations present when it starts, including a
;     call whose head is resolved from a variable. SWI's translated caller
;     retains its earlier argument/guard plan: a later direct call sees
;     one signature here, while eval at the same point sees both. A
;     let-bound head can omit the declaration's guards entirely in SWI.
;     These are reference translation bugs; refreshing the call plan on
;     declaration-generation changes is a repair candidate.
;     Decided: Zar, 2026-10-01, preserve live declarations.
(: visibility-a VisibilityA)
(: visibility-a VisibilityB)
(: visibility-f (-> VisibilityA %Undefined%))
(= (visibility-f $x) (Visible $x))
(= (visibility-caller)
   (progn (add-atom &self (: visibility-f (-> VisibilityB %Undefined%)))
          (collapse (visibility-f visibility-a))))
; SWI-PeTTa, both qualified pins: ((Visible visibility-a))
!(visibility-caller)
; SWI-PeTTa, both qualified pins: ((Visible rejected))
!(let $f visibility-f (collapse ($f rejected)))
(: visibility-raw (-> Atom %Undefined%))
(= (visibility-raw $x) (Raw $x))
; SWI-PeTTa, both qualified pins: (Raw 3)
!(let $f visibility-raw ($f (+ 1 2)))
; A head held by a variable or a parameter, foldl's step included, also
; meets the declaration's result check.  Stable-fixed SWI-PeTTa c3adb86
; (2026-11-05) answers the unchecked result on these routes, while the
; direct call (visibility-step 1 0) answers nothing in both engines.
(: visibility-step (-> Number Number Number))
(= (visibility-step $item $acc) rejected)
(= (visibility-apply $f $a $b) ($f $a $b))
; SWI-PeTTa stable-fixed c3adb86: (rejected)
!(collapse (let $f visibility-step ($f 1 0)))
; SWI-PeTTa stable-fixed c3adb86: (rejected)
!(collapse (visibility-apply visibility-step 1 0))
; SWI-PeTTa stable-fixed c3adb86: (rejected)
!(collapse (foldl visibility-step (1) 0))

; 15. Rows that repeat one declaration are one declaration.  A call answers
;     once for them in both implementations; SWI's get-type lists every row,
;     CeTTa's lists the declaration once, so the version a typed relation
;     keeps does not depend on a repeated row.
;     Decided: Zar, 2026-11-05 ("duplicated declarations should probably be
;     idempotent. Different declarations are another matter").
(: repeated-row (-> Number Number))
(: repeated-row (-> Number Number))
; SWI-PeTTa: ((-> Number Number) (-> Number Number))
!(collapse (get-type repeated-row))

; 16. The atom -inf is the constant minus infinity, the dual of inf: the
;     identity of max, as inf is of min.  SWI-PeTTa's reader leaves it an
;     atom its arithmetic evaluator rejects ('-inf'/0 is not a function), an
;     accident no program relies on.
;     Decided: Zar, 2026-11-06 ("we should not treat stupid conformance to
;     every silly edge case almost no one writes programs over").
; SWI-PeTTa: an evaluation error for each
!(min -inf 1)
!(max -inf 1)
!(< -inf 1)

; 17. min and max select one of their operands by the numeric order of what
;     they stand for: a constant stays itself, integers and floats compare
;     exactly, -0.0 is below 0 and 0.0, a NaN operand propagates, and on a tie
;     min answers its first operand and max its second, so (min a b) and
;     (max a b) are a stable sort of a and b.  SWI-PeTTa computes them as
;     arithmetic (R is min(A,B)): a constant becomes its float, and a tie of
;     an integer and a float is the float for both.
;     Decided: Zar, 2026-11-06 ("(== pi (min pi 4)) --> false ... We must
;     dynamically maintain our living petta specs and divergence graph").
; SWI-PeTTa: 3.141592653589793, false, 2.0, 2.0
!(min pi 4)
!(== pi (min pi 4))
!(min 2 2.0)
!(max 2.0 2)

; 18. A constructor field declared _ is unchecked by get-type, as one
;     declared %Undefined% is, and as an argument declared _ is unguarded in
;     a typed call: _ reads the same to the call protocol and to the type
;     relation.  SWI-PeTTa's get-type reads it there as the type named _, so
;     only a value declared _ fills the field, and (LooseBox "w") is typed by
;     its tuple alone.  The reference's own comment on that check reads "As in
;     typed calls, an argument declared %Undefined% is not checked", and its
;     typed calls leave _ unguarded as they leave %Undefined%; the check tests
;     %Undefined% alone.  Decided by the coherence rule (Zar, 2026-11-06).
; SWI-PeTTa: ((-> _ LooseBoxT) String), ()
(: LooseBoxT Type)
(: LooseBox (-> _ LooseBoxT))
(: loose-size (-> LooseBoxT Number))
(= (loose-size (LooseBox $x)) 1)
!(get-type (LooseBox "w"))
!(collapse (loose-size (LooseBox "w")))

; Python boolean interchange (qualified separately by test-python-objects,
; since this fixture also runs without Python): true/false argument values
; become Python bool, including in legacy py-call. Janus's default atom
; conversion makes both truthy strings. Quoted strings remain strings.
; Upstream repair candidate: encode boolean leaves as Janus boolean terms
; before invocation, recursively in positional and keyword arguments.
; Decided: boolean round-trip semantics, Zar's review handoff, 2026-11-06.
