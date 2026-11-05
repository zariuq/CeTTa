; A body translated before a function is installed calls it once it is
; installed: a plain occurrence, an argument and the operand of call, eval
; or reduce all reach the later definition, and a replacement of it.  Quoted
; code stays data: eval reinterprets a quoted term as code, while reduce and
; call keep a quotation as written.
(= (identity $x) $x)
(= (old-data) (late 5))
(= (old-id) (id (late 5)))
(= (old-quote) (quote (late 5)))
(= (old-call) (call (late 5)))
(= (old-eval) (eval (late 5)))
(= (old-reduce) (reduce (late 5)))
(= (nested-call) (call (identity (late 5))))
(= (nested-eval) (eval (identity (late 5))))
(= (nested-reduce) (reduce (identity (late 5))))
(= (open-call $x) (call (late $x)))
(= (open-eval $x) (eval (late $x)))
(= (open-reduce $x) (reduce (late $x)))
(= (held-eval $x) (eval $x))
(= (held-reduce $x) (reduce $x))
(= (returned-code) (call (identity (quote (late 5)))))

; Plain forward references are independent of the order of definitions.
(= (forward) (later-definition 5))
(= (later-definition $x) (+ $x 1))
!(forward)
!(add-atom &self (= (late $n) (+ $n 1)))
!(old-data)
!(old-id)
!(old-quote)
!(old-call)
!(old-eval)
!(old-reduce)
!(nested-call)
!(nested-eval)
!(nested-reduce)
!(open-call 7)
!(open-eval 7)
!(open-reduce 7)
!(held-eval (quote (identity (late 5))))
!(held-reduce (quote (identity (late 5))))
!(returned-code)

; Cached compiled bodies must observe a later replacement of the callee.
!(remove-atom &self (= (late $n) (+ $n 1)))
!(add-atom &self (= (late $n) (+ $n 2)))
!(old-call)
!(old-eval)
!(old-reduce)
!(nested-call)
!(nested-eval)
!(nested-reduce)
!(old-data)

; Two equations with the same answer contribute two occurrences.
!(add-atom &self (= (late $n) (+ $n 2)))
!(msort (collapse (old-call)))
!(msort (collapse (old-eval)))
!(msort (collapse (old-reduce)))

; A raising dispatch is caught by reduce, whereas a direct call raises.
(= (old-fail-call) (call (later-error)))
(= (old-fail-reduce) (reduce (later-error)))
!(add-atom &self (= (later-error) (/ 1 0)))
!(catch (old-fail-call))
!(collapse (old-fail-reduce))
