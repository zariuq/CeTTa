; `lang:` judgments expose the language inventory, parsing and printing as
; ordinary data operations.  The native diamond type over Prime's own
; evaluation is realized through the same authority as `type:may`; requests
; for other languages, and every step request, name a registered language
; and remain inert until an admitted checker realizes them.  An unregistered
; language is a refutation.

!(lang:languages)
!(lang:parse "(= (f x) x) (: f (-> A A))")
!(lang:print (-> set prop))
(: Nat (u 0))
(: z Nat)
(= (f) z)
!(lang:native-type prime (f) Nat)
!(lang:native-type prime z Nat)
!(lang:native-type prime (f) set)
!(lang:native-type zero (eval a) Nat)
!(lang:native-type nosuch a b)
!(lang:step zero (eval a))
