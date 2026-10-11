; Private-name invariance.  This file and private_names_renamed.metta hold
; the same queries; each loads one of two libraries that differ only in how
; every variable and binder of every clause is spelled
; (tests/prime/scope_spectrum/lib/scope_lib_private_names*.metta).  The two
; expected outputs are one text, byte for byte: no private spelling may
; change an answer.  Every answer is ground or holds only this file's names,
; and several of those are spelled as the library spells its private names.
!(import! &self ../scope_spectrum/lib/scope_lib_private_names)

; ---- A closure the library returns, called twice: each call owns its local.
!(row closure-twice)
!(let $f (lib-closure) (Pair ($f 1) ($f 2)))
!(row closure-same-argument)
!(let $f (lib-closure) (Pair ($f 1) ($f 1)))
!(row closure-outsider)
!(let $f (lib-closure) (Pair ($f 1) $y))
!(row closure-outsider-bound)
!(let $y 5 (let $f (lib-closure) (Pair ($f 1) $y)))

; ---- A map-atom template that owns its local.
!(row template)
!(lib-template (1 2))
!(row template-outsider)
!(let $u 5 (Pair $u (lib-template (1 2))))

; ---- A closure with a crossing set {$t}: $t is the caller's argument, every
; other name is the closure's own.
!(row crossing)
!(let $f (lib-crossing $t) (let $r ($f (d 7 1)) (Pair $t $r)))
!(row crossing-twice)
!(let $f (lib-crossing $t) (Pair ($f (d 7 1)) ($f (d 7 2))))
!(row crossing-conflict)
!(let $f (lib-crossing $t) (Pair ($f (d 7 1)) ($f (d 8 2))))
!(row crossing-outsider)
!(let $f (lib-crossing $t) (let $r ($f (d 7 1)) (Pair $k $r)))

; ---- Cross-form capture: the library's local and the caller's name share a
; spelling here, and not in the renamed copy.
!(row cross-form)
!(let $w 5 (((lib-cross) $w) 1))
!(row cross-form-other-spelling)
!(let $y 5 (((lib-cross) $y) 1))
