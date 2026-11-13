; The decons-atom entry guard refutes an equation only where decons-atom
; would answer nothing: for PeTTa that is any closed value that is not a
; non-empty expression (a symbol, a number, a string, a boolean, a space,
; the empty expression), and it fails there without raising.  The answers
; below equal those of the unguarded evaluator.
(= (cnt ()) 0)
(= (cnt $xs) (let ($h $t) (decons-atom $xs) (+ 1 (cnt $t))))
!(println! (collapse (catch (cnt foo))))
!(println! (collapse (catch (cnt 5))))
!(println! (collapse (catch (cnt "abc"))))
!(println! (collapse (catch (cnt ()))))
!(println! (collapse (catch (cnt (())))))
(= (head-of ()) zero)
(= (head-of $xs) (let ($h $t) (decons-atom $xs) $h))
!(println! (collapse (catch (head-of foo))))
!(println! (collapse (catch (head-of 1.5))))
!(println! (collapse (catch (head-of True))))
!(println! (collapse (catch (head-of &self))))
!(println! (collapse (catch (head-of (a b)))))
; The guard reads the argument decons-atom destructures, wherever the head
; binds it.
(= (pick x $b) 0)
(= (pick $a $b) (let ($h $t) (decons-atom $b) $h))
!(println! (sort-atom (collapse (pick x (p q)))))
!(println! (collapse (pick x foo)))
!(println! (collapse (pick foo (p q))))
(= (unwrap (box ())) none)
(= (unwrap (box $xs)) (let ($h $t) (decons-atom $xs) $h))
!(println! (collapse (unwrap (box ()))))
!(println! (collapse (unwrap (box (r s)))))
; Once a closed value's own equation has answered, the guarded equation
; leaves no alternative behind, nor do the equations between whose heads
; refute the value: a loop over each kind keeps a constant choice depth,
; also where each call is reached through and, case and +.
(= (leaf end) 0)
(= (leaf 7) 0)
(= (leaf "s") 0)
(= (leaf True) 0)
(= (leaf ()) 0)
(= (leaf $xs) (let ($h $t) (decons-atom $xs) 1))
(= (leaves) (+ (leaf end) (+ (leaf 7) (+ (leaf "s") (+ (leaf True) (leaf ()))))))
(= (tick) (and True (case (leaves) ((0 True) ($other False)))))
(= (loop $n) (if (== $n 0) done (let $_ (tick) (loop (- $n 1)))))
!(println! (loop 200))
; Heads alone refute the remaining equations: another literal, another
; length, another child.
(= (kind (pair a b)) 1)
(= (kind (pair c d)) 2)
(= (kind (single a)) 3)
(= (kind 7) 4)
(= (kinds) (+ (kind (pair a b)) (+ (kind (pair c d)) (+ (kind (single a)) (kind 7)))))
(= (tock) (and True (case (kinds) ((10 True) ($other False)))))
(= (spin $n) (if (== $n 0) done (let $_ (tock) (spin (- $n 1)))))
!(println! (spin 200))
; A later equation whose head matches keeps its answer: none is lost.
(= (two end) 0)
(= (two 7) 5)
(= (two $x) 1)
!(println! (sort-atom (collapse (two end))))
!(println! (sort-atom (collapse (two 7))))
!(println! (sort-atom (collapse (two other))))
(= (num 7) a)
(= (num 7.0) b)
(= (num $x) c)
!(println! (sort-atom (collapse (num 7))))
!(println! (sort-atom (collapse (num 7.0))))
