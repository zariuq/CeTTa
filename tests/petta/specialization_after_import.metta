; A specialization depends on the equations of the function it was derived
; from and, where it calls a callee directly because that callee raises
; nothing, on the callee's equations too.  An import adds equations without
; reporting each one, so it invalidates the specializations of its space.

(= (app1 $f $a) ($f $a))
(= (safe $x) $x)

!(msort (collapse (app1 safe 1)))
!(import! &self specialization_after_import_lib)

; The imported equation of app1 is seen, and the imported raising equation
; of safe is under the handler of the dispatch app1 made.
!(msort (collapse (app1 safe 1)))

; A direct call is not a dispatch.
!(catch (collapse (safe 1)))
