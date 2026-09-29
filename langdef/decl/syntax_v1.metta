; Declarations of named arithmetic expressions: `name = expression ;`, where
; an expression is a sum of numbers, names and calls `name(expression)`.  A
; call is data here: importing a file builds its program; running it is a
; separate step.
(gslt-presentation-v1 DeclV1
  (signature
  )
  (equations)
  (rewrites
    (rule def-decl-file (head (definition decl-file (node decl-file (right (ref decl-skip) (until eof (ref decl)))))) (body))
    (rule def-decl (head (definition decl (node decl (seq (left (ref name) (ref k-eq)) (left (ref sum) (ref k-semi)))))) (body))
    (rule def-sum (head (definition sum (node sum (seq (ref app) (star (right (ref k-plus) (ref app))))))) (body))
    (rule def-app (head (definition app (alt (node call (seq (ref name) (right (ref k-open) (left (ref sum) (ref k-close))))) (ref atom)))) (body))
    (rule def-atom (head (definition atom (alt (node number (ref digits)) (node ref (ref name))))) (body))
    (rule def-name (head (definition name (left (ref name-raw) (ref decl-skip)))) (body))
    (rule def-name-raw (head (definition name-raw (node name (seq (class ascii-lower) (star (class (c-union ascii-lower ascii-digit))))))) (body))
    (rule def-digits (head (definition digits (left (ref digits-raw) (ref decl-skip)))) (body))
    (rule def-digits-raw (head (definition digits-raw (node digits (plus (class ascii-digit))))) (body))
    (rule def-k-eq (head (definition k-eq (left (lit (cons (cp 61) nil)) (ref decl-skip)))) (body))
    (rule def-k-semi (head (definition k-semi (left (lit (cons (cp 59) nil)) (ref decl-skip)))) (body))
    (rule def-k-plus (head (definition k-plus (left (lit (cons (cp 43) nil)) (ref decl-skip)))) (body))
    (rule def-k-open (head (definition k-open (left (lit (cons (cp 40) nil)) (ref decl-skip)))) (body))
    (rule def-k-close (head (definition k-close (left (lit (cons (cp 41) nil)) (ref decl-skip)))) (body))
    (rule def-decl-skip (head (definition decl-skip (star (class tptp-whitespace)))) (body))
  ))
