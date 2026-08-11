(: undefined-output (-> Number %Undefined%))
(= (undefined-output $x) (+ $x 42))

(: atom-output (-> Number Atom))
(= (atom-output $x) (+ $x 42))

(: atom-input-output (-> Atom Atom))
(= (atom-input-output $x) (+ $x 42))

(: expression-input-output (-> Expression Expression))
(= (expression-input-output $x) (pair $x))

!(undefined-output (+ 1 1))
!(atom-output (+ 1 1))
!(atom-input-output (+ 1 1))
!(expression-input-output (quote (nested 2)))
