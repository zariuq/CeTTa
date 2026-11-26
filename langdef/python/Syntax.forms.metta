; The reader of Python 3.13 (theory Python of Syntax.module): every production
; denotes (Label child ...), which Ast.module maps to CPython's ast nodes, so
; no templates are declared.
(pres:version python "1.0")
(pres:forms python ((File (cs:arg 0))))
(pres:statement python "Stmt" "\n")
(pres:leaf-category python "Exp")
(pres:token-default python text)
(pres:positions python)
(pres:lists python cons)
(pres:full-text python FieldHeadDebug)
(pres:full-text python FieldHeadPlain)
; The reader of eval()'s input (entry EvalInput of the same theory): its one
; expression list, as Ast.module's FEval reads it.
(pres:version python-eval "1.0")
(pres:forms python-eval ((Eval (cs:arg 0))))
(pres:statement python-eval "Stmt" "\n")
(pres:leaf-category python-eval "Exp")
(pres:token-default python-eval text)
(pres:positions python-eval)
(pres:lists python-eval cons)
(pres:full-text python-eval FieldHeadDebug)
(pres:full-text python-eval FieldHeadPlain)
