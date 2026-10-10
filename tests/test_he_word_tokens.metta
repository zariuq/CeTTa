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
