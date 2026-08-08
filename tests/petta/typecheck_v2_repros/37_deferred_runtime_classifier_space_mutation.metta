; A relational proof receipt is invalid as soon as its classifier equation is
; removed.  Reusing the previously guarded value must reject before a later
; mutation could weaken or replace the missing authority.

(= (get-type foo) RuntimeOnly)

!(let $typed (the RuntimeOnly $value)
      (let foo $value
        (let
          (remove-atom &self (= (get-type foo) RuntimeOnly))
          $_removed
          (let
            (add-atom &self (= (get-type foo) Other))
            $_added
            $typed))))
