; Python's core: the abstract syntax of the ast stage, with the symbol tables
; the scope program gives it, desugared by Core.module's compiled program
; (generated/core_program_v1.metta), whose host relations ask host_core.metta
; and the scope and ast stages' hosts, which that program links.  The core
; stage of langdef.metta: its value is the module's code object.
!(import! &self host_core)
!(import! &self scope)
!(import! &self generated/core_program_v1)
!(import! &self str)
!(import! &self fs)
!(import! &self system)

; a compilation: its SyntaxWarnings are the AST preprocessor's, found before
; the symbol tables are built, then the code generator's (&py-compile-
; warnings, host_core.metta "compile warnings")
(= (python:core-of-ast $ast $file)
   (let $fw (pyhost:finally-warnings $ast)
     (let $u (change-state! &py-compile-warnings $fw)
       (case (collapse (gslt:entry:python-scope:rewrite:10 (quote (SScope $ast))))
         ((((gslt:result:python-scope:rewrite:10 (st:fail (st:err $why $line $col $eline $ecol))))
           (Error (python:scope) (python:syntax-error $why $line $col $eline $ecol)))
          (((gslt:result:python-scope:rewrite:10 $table)) (python:core-of $ast $table $file))
          ($other (Error (python:scope) "no symbol table")))))))

; the core stage of the program run: its compilation's warnings are shown as
; CPython shows them while it compiles (Python/_warnings.c with the default
; filters, which show every SyntaxWarning, and warnings._formatwarnmsg_impl):
; FILE:LINE: SyntaxWarning: MESSAGE, then the line's text stripped
(= (python:core-of-main $ast)
   (let $code (python:core-of-ast $ast (python:main-source))
     (let $ws (get-state &py-compile-warnings)
       (let $u (if (== $ws ()) () (python:show-warnings (python:main-source) $ws)) $code))))
; the main program's file as CPython names it (config_run_filename_abspath):
; a relative path joined to the working directory as written, unnormalized
(= (python:main-source)
   (let $ps (collapse (match &self (pipeline:current-source $p) $p))
     (if (== $ps ()) "" (python:absolute-path (car-atom $ps)))))
(= (python:absolute-path $p)
   (if (== $p "") $p
     (if (== (str:byte-slice $p 0 1) "/") $p (str:concat (str:concat (fs:canonical-path ".") "/") $p))))
(= (python:show-warnings $file $ws)
   (if (== $ws ()) ()
     (let (c:syntax-warning $m $l $c $el $ec) (car-atom $ws)
       (let $u (system:write stderr (python:warning-text $file $l "SyntaxWarning" $m))
         (python:show-warnings $file (cdr-atom $ws))))))
(= (python:warning-text $file $l $category $m)
   (let $head (str:concat (str:concat (str:concat $file ":") (str:concat (str:number->string $l) ": "))
                          (str:concat (str:concat $category ": ") (str:concat $m "\n")))
     (case (python:file-line $file $l)
       (((line $t) (str:concat $head (str:concat (str:concat "  " (str:trim-props 3848294891520 "both" $t)) "\n")))
        ($none $head)))))
; linecache.getline: line L of a file as text, (line TEXT), or (no-line)
(= (python:file-line $file $l)
   (if (fs:exists $file)
       (let $lines (str:split "\n" (fs:read-text $file))
         (if (if (>= $l 1) (<= $l (size-atom $lines)) false)
             (let $t (index-atom $lines (- $l 1))
               (if (if (== $l (size-atom $lines)) (== $t "") false) (no-line) (line $t)))
             (no-line)))
       (no-line)))

; a compilation's names are interned before its code objects are made, and
; its constants after (host_core.metta "interned strings"); under from
; __future__ import annotations its annotations are compiled as their text
; (host_core.metta "from __future__ import annotations")
; its code objects, each nested one too, carry FILE (co_filename)
(= (python:core-of $ast $table $file)
   (let $n (add-atom &py-interned (py:pending-names $ast))
     (let $fm (pyhost:import-modes (pyhost:future-module $ast))
     (case (collapse (gslt:entry:python-core:rewrite:10 (quote (CModule $fm $table))))
       ((((gslt:result:python-core:rewrite:10 $code))
         (let $r (python:checked $code (repr $code))
           (if (== (get-metatype $r) Expression)
               (if (== (car-atom $r) c:code) (let $c (add-atom &py-interned (py:pending-constants $r)) (pyhost:with-file $r $file)) $r)
               $r)))
        (() (Error (python:core) "no core"))
        ($many (Error (python:core) "more than one core")))))))

; a core holding compile errors, (c:syntax-error MESSAGE LINE COL ELINE ECOL)
; where CPython's compiler raises one, is refused with the first in the
; core's order, which is the order the compiler meets them; TEXT is the
; core's written form
(= (python:checked $code $text)
   (let $cw (if (pyrt:is-int (str:find $text "c:syntax-warning")) (pyhost:core-warnings $code) ())
     (let $u (if (== $cw ()) () (let $fw (get-state &py-compile-warnings) (change-state! &py-compile-warnings (pyhost:rev-onto (pyhost:rev-onto $fw ()) $cw))))
       (let $code1 (if (pyrt:is-int (str:find $text "c:syntax-warning")) (pyhost:without-warnings $code) $code)
         (if (pyrt:is-int (str:find $text "c:syntax-error"))
             (case (pyhost:first-error $code1)
               (((c:syntax-error $msg $l $col $el $ec) (Error (python:core) (python:syntax-error $msg $l $col $el $ec)))
                ($none (python:constants $code1 $text))))
             (python:constants $code1 $text))))))
; the sets of folded set displays as the compilation's constant cache keeps
; them (pyhost:merge-const-sets)
(= (python:constants $code $text)
   (if (pyrt:is-int (str:find $text "c:const-set")) (pyhost:merge-const-sets $code) $code))
