!(bind! &open-existence-space (new-space))
!(add-atom &open-existence-space (open-key $stored))

; A stored-side variable can match a ground query without being structurally
; equal to it, so exact-membership fusion must decline.
!(== ()
     (collapse
       (once
         (match &open-existence-space (open-key value) hit))))

!(bind! &template-existence-space (new-space))
!(add-atom &template-existence-space (key a))

; A computed empty template suppresses the matching row.  Existence of the
; row alone therefore cannot decide collapse emptiness.
!(== ()
     (collapse
       (once
         (match &template-existence-space (key a) (empty)))))

!(add-atom &template-existence-space (value ()))

; A match-bound template can itself be empty and is likewise outside the
; exact constant-template fragment.
!(== ()
     (collapse
       (once
         (match &template-existence-space (value $item) $item))))
