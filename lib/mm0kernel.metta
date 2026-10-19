; Native MM0 verifier. Import with (import! &self mm0kernel).
; A theory is an append-only list of checked MM0 declarations.  Theorems are
; opaque seals: only a checked MMB file, an admitted axiom or a checked proof
; DAG creates one.  Declarations, statements and proof rows are ordinary data.
; See docs/mm0_native.txt for the data forms and the ownership contract.

(: mm0:theory (-> Atom))
(: __cetta_lib_mm0_theory (-> Atom Atom))
(= (mm0:theory)
   (function (chain (eval (__cetta_lib_mm0_theory empty)) $result (return $result))))

(: mm0:load-mmb (-> %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_mm0_load-mmb (-> Atom Atom Atom Atom))
(= (mm0:load-mmb $th $mmb $spec)
   (function (chain (eval (__cetta_lib_mm0_load-mmb $th $mmb $spec)) $result (return $result))))

(: mm0:check-mmb (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_mm0_check-mmb (-> Atom Atom Atom))
(= (mm0:check-mmb $mmb $spec)
   (function (chain (eval (__cetta_lib_mm0_check-mmb $mmb $spec)) $result (return $result))))

(: mm0:theorem (-> %Undefined% Atom Atom))
(: __cetta_lib_mm0_theorem (-> Atom Atom Atom))
(= (mm0:theorem $th $name)
   (function (chain (eval (__cetta_lib_mm0_theorem $th $name)) $result (return $result))))

(: mm0:statement (-> %Undefined% Atom))
(: __cetta_lib_mm0_statement (-> Atom Atom))
(= (mm0:statement $thm)
   (function (chain (eval (__cetta_lib_mm0_statement $thm)) $result (return $result))))

(: mm0:same-declaration (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_mm0_same-declaration (-> Atom Atom Atom))
(= (mm0:same-declaration $a $b)
   (function (chain (eval (__cetta_lib_mm0_same-declaration $a $b)) $result (return $result))))

(: mm0:declare (-> %Undefined% Atom Atom))
(: __cetta_lib_mm0_declare (-> Atom Atom Atom))
(= (mm0:declare $th $decl)
   (function (chain (eval (__cetta_lib_mm0_declare $th $decl)) $result (return $result))))

(: mm0:prove (-> %Undefined% Atom Atom Atom %Undefined% Atom))
(: __cetta_lib_mm0_prove (-> Atom Atom Atom Atom Atom Atom))
(= (mm0:prove $th $statement $dummies $rows $root)
   (function (chain (eval (__cetta_lib_mm0_prove $th $statement $dummies $rows $root)) $result (return $result))))

(: mm0:stats (-> %Undefined% Atom))
(: __cetta_lib_mm0_stats (-> Atom Atom))
(= (mm0:stats $th)
   (function (chain (eval (__cetta_lib_mm0_stats $th)) $result (return $result))))

(: mm0:budget (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_mm0_budget (-> Atom Atom Atom))
(= (mm0:budget $th $steps)
   (function (chain (eval (__cetta_lib_mm0_budget $th $steps)) $result (return $result))))

(: mm0:live (-> Atom))
(: __cetta_lib_mm0_live (-> Atom Atom))
(= (mm0:live)
   (function (chain (eval (__cetta_lib_mm0_live all)) $result (return $result))))
