(= (translated-before-install $x) (late-callable $x $x))

!(translated-before-install 1)

!(add-atom &self
   (= (late-callable $left $right) (+ $left $right)))

; The earlier equation keeps the data interpretation fixed when it was
; translated.  A fresh request is translated against the newer definition.
!(translated-before-install 1)
!(late-callable 1 1)
