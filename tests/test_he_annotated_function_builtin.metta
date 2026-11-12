; An Atom-typed annotation of the function builtin does not stop the block
; from running. The expected output agrees with Hyperon Experimental 0.2.10.
(: function (-> Atom Atom))
!(function (return 7))
