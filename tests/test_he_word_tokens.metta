; A bound word is read into the forms that follow it, never into a form that
; was read before the binding.
(= (f) &kb)
(= (g) zz)
!(bind! &kb 5)
!(bind! zz 6)
!(f)
!(g)
! &kb
! zz
; An included file is read form by form with the words bound so far, and the
; words it binds stay in effect after it.
!(include support/he_word_tokens_included)
! inc-word
!(match &self (inc-fact $x) $x)

; Substitution inserts an already-read value once, including an unbound
; name stored as that value. It does not tokenize the inserted value again.
!(bind! &word-first &word-second)
!(bind! &word-second 11)
! &word-first

; Old callable and space spellings do not acquire later bindings. A form
; read after the space binding retains its actual space value.
(= (word:old-call) (&word-late-call 3))
(= (word:old-space) &word-late-space)
!(bind! &word-late-call +)
!(bind! &word-late-space (new-space))
!(add-atom &word-late-space word-entry)
!(word:old-call)
!(case (get-atoms (word:old-space))
   (((Error $source $reason) refused) ($value unexpected)))
(= (word:captured-space) (get-atoms &word-late-space))
!(word:captured-space)
