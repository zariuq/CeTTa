; If/then/else — uses HE stdlib builtin 'if'
; Do NOT redefine (= (if True $t $e) $t) — that shadows the builtin
; and produces duplicate results.

; For CeTTa: if is NOT a builtin, it needs equations in space.
; So we define it with a custom name to avoid shadowing.
(= (my-if True $then $else) $then)
(= (my-if False $then $else) $else)

!(assertEqual (my-if True yes no) yes)
!(assertEqual (my-if False yes no) no)
!(assertEqual (my-if (== 1 1) same different) same)
!(assertEqual (my-if (== 1 2) same different) different)
!(assertEqual (my-if (< 3 5) less greater) less)
