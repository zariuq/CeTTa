; An undeclared function parameter is used once where Number is required and
; once where String is required.  PeTTa's inferred signatures are positive
; knowledge, not author-written contracts: the conflicting parameter must be
; published as unknown rather than making the clause itself ill-typed.  The
; String result still gives strict mode enough information to type the head.

(: require-number (-> Number Number))
(= (require-number $value) $value)

(: require-string (-> String String))
(= (require-string $value) $value)

(= (conflicting-use $value)
   (let $ignored (require-number $value)
     (require-string $value)))
