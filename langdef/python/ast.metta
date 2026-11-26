; Python's abstract syntax, as CPython's ast module gives it: the statements
; the python presentation (Syntax.module) reads, folded by Ast.module's
; compiled program (generated/ast_program_v1.metta), whose host relations ask
; host_literals.metta.  The ast stage of langdef.metta.  A text the reader
; refuses, or one with no abstract syntax (an invalid target, say), is an
; (Error WHERE WHY).
!(import! &self syntax)
!(import! &self fs)
!(import! &self host_literals)
; how a refused program is reported
!(import! &self refusal)
!(import! &self generated/ast_program_v1)

; the fold's answers, collapsed: an invalid program has none
(= (python:ast-of-cst $stmts)
   (case (collapse (gslt:entry:python-ast:rewrite:10 (quote (FMod $stmts))))
     ((((gslt:result:python-ast:rewrite:10 $ast)) $ast)
      ($other (Error (python:ast) "invalid syntax")))))

(= (python:ast-of-text $text)
   (let $cst (syntax:read-in "python" $text)
     (case $cst
       (((Syntax:Fault $kind $why) (Error (python:read) $why))
        ($stmts (python:ast-of-cst $stmts))))))

(= (python:ast-of-file $path) (python:ast-of-text (fs:read-text $path)))

; eval()'s input, read by the presentation python-eval (the entry EvalInput):
; the reader gives an entry that is not a list as its node's items, rebuilt
; into the node here
(= (python:ast-eval-of-text $text)
   (let $cst (syntax:read-in "python-eval" $text)
     (case $cst
       (((Syntax:Fault $kind $why) (Error (python:read) $why))
        ($items (python:ast-eval-of-cst (python:node-of-items $items)))))))
(= (python:node-of-items $items)
   (case $items
     (((LCons $x $rest) (cons-atom $x (python:node-of-items $rest)))
      ($nil ()))))
(= (python:ast-eval-of-cst $node)
   (case (collapse (gslt:entry:python-ast:rewrite:10 (quote (FEval $node))))
     ((((gslt:result:python-ast:rewrite:10 $ast)) $ast)
      ($other (Error (python:ast) "invalid syntax")))))
