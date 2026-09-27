; Every place where plain PeTTa on CeTTa deliberately answers differently from
; SWI-PeTTa.  Each entry states SWI-PeTTa's answer, the reason CeTTa differs,
; and who decided it when.  CeTTa's answers are pinned in the .expected file,
; so an entry that drifts is noticed; a difference missing from this file is
; found only by running both engines.  SWI-PeTTa's answers are those of its
; revision ae66fa8e.  An entry leaves this file only when the two engines agree.

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
