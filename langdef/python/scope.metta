; Python's symbol tables, as CPython's symtable module gives them: the
; abstract syntax of the ast stage, analyzed by Scope.module's compiled
; program (generated/scope_program_v1.metta), whose host relations ask
; host_scope.metta, and the ast stage's (its constants), which that
; program links.  The scope stage of langdef.metta.  A program CPython's
; symbol table pass refuses (a name both a parameter and global, say) has no
; tables: an (Error WHERE WHY).
!(import! &self host_scope)
!(import! &self ast)
!(import! &self generated/scope_program_v1)

; each table is (st:otable KIND NAME LINENO SYMBOLS CHILDREN), a symbol
; (st:osym NAME NUMBER), NUMBER its flags and scope as CPython numbers them;
; a refused program's error stands where CPython places it (columns from 1)
(= (python:scope-of-ast $ast)
   (case (collapse (gslt:entry:python-scope:rewrite:10 (quote (SScopeOut $ast))))
     ((((gslt:result:python-scope:rewrite:10 (st:fail (st:err $why $line $col $eline $ecol))))
       (Error (python:scope) (python:syntax-error $why $line $col $eline $ecol)))
      (((gslt:result:python-scope:rewrite:10 $tables)) $tables)
      (() (Error (python:scope) "no symbol table"))
      ($many (Error (python:scope) "more than one symbol table")))))
