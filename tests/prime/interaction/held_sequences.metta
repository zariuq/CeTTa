; Sequence observers read stored values without exposing private carriers.
!(import! &self interaction)
!(bind! &sequences (new-space))
!(add-atom &sequences (members (a b a)))
!(assertEqual (transform &sequences (read (members $m))
   (union-atom $m (c))) (a b a c))
!(assertEqual (transform &sequences (read (members $m))
   (union-atom (c) $m)) (c a b a))
!(assertEqual (transform &sequences (read (members $m))
   (intersection-atom $m (a))) (a))
!(assertEqual (transform &sequences (read (members $m))
   (subtraction-atom $m (a))) (b a))
!(assertEqual (transform &sequences (read (members $m))
   (unique-atom $m)) (a b))
!(assertEqual (transform &sequences (read (members $m))
   (sort-atom $m)) (a a b))

; Code-shaped elements and Error-shaped sequence values remain data.
!(bind! &sequence-effects (new-space))
!(add-atom &sequences
   (programs (Error (add-atom &sequence-effects hijacked) reason)))
!(assertEqual (transform &sequences (read (programs $m))
   (get-metatype (unique-atom $m))) Expression)
!(assertEqual (transform &sequences (read (programs $m))
   (size-atom (union-atom $m (tail)))) 4)
!(assertEqual (transform &sequences (read (programs $m))
   (size-atom (subtraction-atom $m (reason)))) 2)
!(assertEqual (collapse (get-atoms &sequence-effects)) ())

; Native validation failures keep their error role.
!(assertEqual (get-metatype
   (transform &sequences (read (members $m)) (sort-numbers-atom $m))) Expression)
