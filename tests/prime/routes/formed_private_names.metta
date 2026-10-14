; Formed-name invariance.  This file and formed_private_names_renamed.metta
; hold the same queries; each loads one of two libraries that differ only in
; how every variable and binder of every clause is spelled
; (tests/prime/scope_spectrum/lib/scope_lib_formed_names*.metta).  Every
; closure there is formed while the program runs: by cons-atom, by opening
; written code with `*`, and by opening code that `lift let` filled.  The
; two expected outputs are one text, byte for byte: no private spelling may
; change an answer.  Every answer is ground or holds only this file's names,
; and several of those are spelled as the library spells its private names.
!(import! &self ../scope_spectrum/lib/scope_lib_formed_names)

; ---- A formed closure, called twice: each call owns its local.
!(row cons-closure-twice)
!(let $f (lib-cons-closure) (Pair ($f 1) ($f 2)))
!(row star-closure-twice)
!(let $f (lib-star-closure) (Pair ($f 1) ($f 2)))
!(row lift-let-closure-twice)
!(let $f (lib-lift-let-closure) (Pair ($f 1) ($f 2)))
!(row cons-closure-outsider)
!(let $f (lib-cons-closure) (Pair ($f 1) $y))
!(row star-closure-outsider)
!(let $f (lib-star-closure) (Pair ($f 1) $y))
!(row lift-let-closure-outsider)
!(let $f (lib-lift-let-closure) (Pair ($f 1) $y))
!(row cons-closure-outsider-bound)
!(let $y 5 (let $f (lib-cons-closure) (Pair ($f 1) $y)))
!(row star-closure-outsider-bound)
!(let $y 5 (let $f (lib-star-closure) (Pair ($f 1) $y)))
!(row lift-let-closure-outsider-bound)
!(let $y 5 (let $f (lib-lift-let-closure) (Pair ($f 1) $y)))

; ---- Cross-form capture: the library's local and the caller's name share a
; spelling here, and not in the renamed copy.
!(row cons-cross-form)
!(let $w 5 (((lib-cons-cross) $w) 1))
!(row cons-cross-form-other-spelling)
!(let $y 5 (((lib-cons-cross) $y) 1))
!(row star-cross-form)
!(let $w 5 (((lib-star-cross) $w) 1))
!(row star-cross-form-other-spelling)
!(let $y 5 (((lib-star-cross) $y) 1))
!(row lift-let-cross-form)
!(let $w 5 (((lib-lift-let-cross) $w) 1))
!(row lift-let-cross-form-other-spelling)
!(let $y 5 (((lib-lift-let-cross) $y) 1))

; ---- A template whose code holds a lambda that owns its local, opened for
; each element.
!(row template)
!(lib-template (1 2))
!(row template-outsider)
!(let $u 5 (Pair $u (lib-template (1 2))))
!(row template-outsider-other-spelling)
!(let $v 5 (Pair $v (lib-template (1 2))))
