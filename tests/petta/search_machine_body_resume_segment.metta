(= (duplicate) 1)
(= (duplicate) 1)

(= (ordered) 2)
(= (ordered) 3)

(= (left) a)
(= (left) b)
(= (right a) 10)
(= (right a) 11)
(= (right b) 20)

(= (none) (empty))

; Resuming the body in the same machine transition must not duplicate or
; suppress effects that occur on the ordinary producer side of the hole.
!(bind! body-resume-state (new-state 0))
(= (effect-source)
   (let $_
     (change-state! body-resume-state
       (+ (get-state body-resume-state) 1))
     7))

; One variable query retains all authored clauses.  The bodies exercise
; distinct continuation constructors around open, duplicate-producing, and
; empty holes; none is a benchmark-specific relation shape.
(= (exercise let)
   (let $value (duplicate) (L $value)))
(= (exercise chain)
   (chain (ordered) $value (C $value)))
(= (exercise let-star)
   (let* (($side (left))
          ($value (right $side)))
     (S $side $value)))
(= (exercise empty)
   (let $value (none) (BAD $value)))
(= (effect-exercise)
   (let $value (effect-source) (E $value)))

!(exercise $kind)
!(effect-exercise)
!(get-state body-resume-state)
