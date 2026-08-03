!(bind! &probe-space (new-space))
!(println! "before")
!(println! (let $s &probe-space
               (let* (($a (add-atom $s (foo 1)))
                      ($b (add-atom $s (foo 2))))
                    (collapse (match $s (foo $x) $x)))))
!(println! "after")
