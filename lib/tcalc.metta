; The transform calculus, tcalc, as a module of MeTTa: (import! &self tcalc).
; tcalc is a moving target, a family of transform steps that differ in a few
; choices; every answer starts with the interpretation it was computed under.
; A program is an expression of its forms as a .tcalc file writes them,
; ((step one) (token 1) (rule a (take (token $x)) (make (got a $x)))).
; A profile is a string of axis settings, as --profile takes it; "" for none.
; A variable in a row is an unknown of the state; an unknown in an answer is
; the variable $uN.  A refusal is an Error with the runner's message.
; See docs/tcalc.md, section 9.

; The program of a file, in tcalc's own, f1r3lang or CHR notation (by suffix).
(: tcalc:load (-> %Undefined% Atom))
(: __cetta_lib_tcalc_load (-> Atom Atom))
(= (tcalc:load $path)
   (function (chain (eval (__cetta_lib_tcalc_load $path)) $result (return $result))))

; A run, as cetta --lang tcalc prints it: per interpretation its line, then
; (outcome ROW ...) per outcome, (ambiguous ...) and (explored ...).
(: tcalc:outcomes (-> Atom %Undefined% Atom))
(: __cetta_lib_tcalc_outcomes (-> Atom Atom Atom))
(= (tcalc:outcomes $program $profile)
   (function (chain (eval (__cetta_lib_tcalc_outcomes $program $profile)) $result (return $result))))

; One step from a state, an expression of rows: per interpretation its line,
; then (successor ROW ...) per state reached and (stepped ...).
(: tcalc:step (-> Atom %Undefined% Atom Atom))
(: __cetta_lib_tcalc_step (-> Atom Atom Atom Atom))
(= (tcalc:step $program $profile $state)
   (function (chain (eval (__cetta_lib_tcalc_step $program $profile $state)) $result (return $result))))

; In each outcome, the rows a read of the pattern can select: per
; interpretation its line, then (answers (outcome ROW ...) ROW ...).
(: tcalc:query (-> Atom %Undefined% Atom Atom))
(: __cetta_lib_tcalc_query (-> Atom Atom Atom Atom))
(= (tcalc:query $program $profile $pattern)
   (function (chain (eval (__cetta_lib_tcalc_query $program $profile $pattern)) $result (return $result))))

; Evaluate CALL in SPACE through demand-driven tcalc rules guarded by per-head
; version counters, preserving exact MeTTa answer multiplicities and returning
; each answer directly. Undefined heads stay data and definitions outside the
; compiled fragment run on the interpreter.
(: tcalc:space-eval (-> %Undefined% Atom Atom))
(: __cetta_lib_tcalc_space_eval (-> Atom Atom Atom))
(= (tcalc:space-eval $space $call)
   (function (chain (eval (__cetta_lib_tcalc_space_eval $space $call)) $result (return $result))))

; Return the compiled tcalc program for a space's definitions rooted at HEAD,
; or (tcalc-not-compiled HEAD REASON) when HEAD is outside the compiled fragment.
(: tcalc:space-program (-> %Undefined% Atom Atom))
(: __cetta_lib_tcalc_space_program (-> Atom Atom Atom))
(= (tcalc:space-program $space $head)
   (function (chain (eval (__cetta_lib_tcalc_space_program $space $head)) $result (return $result))))

; Inspect the per-head version-guarded cache for a space and head symbol:
; returns (tcalc-cache-stats (compiles N) (hits M) (deps (h1 h2 ...))).
(: tcalc:space-cache-stats (-> %Undefined% Atom Atom))
(: __cetta_lib_tcalc_space_cache_stats (-> Atom Atom Atom))
(= (tcalc:space-cache-stats $space $head)
   (function (chain (eval (__cetta_lib_tcalc_space_cache_stats $space $head)) $result (return $result))))
