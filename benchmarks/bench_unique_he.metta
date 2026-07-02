; HE-source unique benchmark.
; Translator target: generic HE unique should lower to collapse + unique-atom.

!(collapse (unique (let $x (superpose (1 2 1 3 2)) (pair $x $x))))
