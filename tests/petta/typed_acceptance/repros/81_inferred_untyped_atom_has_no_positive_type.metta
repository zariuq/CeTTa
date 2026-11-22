; An undeclared atom has no explicit value candidate.  Atom is permissive
; compatibility, not positive inferred knowledge, so strict mode requires a
; declared or otherwise inferable result type for the producer.

(= (choose-token) token)

(: consume-atom (-> Atom Bool))
(= (consume-atom $value) True)

(: use-token (-> Bool))
(= (use-token)
   (consume-atom (choose-token)))
