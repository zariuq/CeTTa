(= (install-and-call)
   (let $_
        (add-atom &self (= (late $x) (+ $x 1)))
        (call (late 4))))

!(install-and-call)
