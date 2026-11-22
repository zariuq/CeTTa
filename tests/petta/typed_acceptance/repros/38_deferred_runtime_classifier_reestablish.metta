; A relational proof receipt is invalid after any Space mutation.  An
; unrelated row leaves the classifier valid, so the value still succeeds,
; but only after a new relational proof is established.

(= (get-type foo) RuntimeOnly)

!(let $typed (the RuntimeOnly $value)
      (let foo $value
        (let
          (add-atom &self (unrelated datum))
          $_added
          $typed)))
