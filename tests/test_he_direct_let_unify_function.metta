; Direct let, unify and function blocks, and the Atom and undefined
; demands of metta. The expected output agrees with Hyperon Experimental
; 0.2.10.
!(let $a 7 $a)
!(let $a 7 (println! 9))
!(unify 7 7 7 Empty)
!(unify 7 7 (println! 9) Empty)
!(eval (unify 7 7 (println! 9) Empty))
!(eval (unify 7 7 7 Empty))
!(function (chain (+ 1 2) $x (return $x)))
!(function (chain (let $a 7 $a) $s (return $s)))
!(function (chain (function (return 4)) $x (return $x)))
!(function (return $result))
!(metta (function (println! 9)) Atom (context-space))
!(metta (eval (let $a 7 $a)) Atom (context-space))
!(metta (let $a 7 (println! 9)) %Undefined% (context-space))
!(metta (function (SomeValue)) Atom &self)
