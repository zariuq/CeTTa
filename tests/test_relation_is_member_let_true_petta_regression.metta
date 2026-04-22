; Direct regression for Petta relation-lowering of let True over builtin membership.

!(test (collapse (let True (is-member $X (1 2 3)) $X))
       (1 2 3))

!(test (collapse (let True (is-member 4 (1 2 3)) 42))
       ())
