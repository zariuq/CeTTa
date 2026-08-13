; A recursive PeTTa chain repeatedly calls one overloaded relation.  Fifteen
; closed signatures are impossible for the ground Type15 argument; the direct
; authority can refute them before the search machine allocates their goals and
; choice-point state.  The untyped/reference route must compute the same Done.

(: typed-chain-choice (-> Type00 Atom))
(: typed-chain-choice (-> Type01 Atom))
(: typed-chain-choice (-> Type02 Atom))
(: typed-chain-choice (-> Type03 Atom))
(: typed-chain-choice (-> Type04 Atom))
(: typed-chain-choice (-> Type05 Atom))
(: typed-chain-choice (-> Type06 Atom))
(: typed-chain-choice (-> Type07 Atom))
(: typed-chain-choice (-> Type08 Atom))
(: typed-chain-choice (-> Type09 Atom))
(: typed-chain-choice (-> Type10 Atom))
(: typed-chain-choice (-> Type11 Atom))
(: typed-chain-choice (-> Type12 Atom))
(: typed-chain-choice (-> Type13 Atom))
(: typed-chain-choice (-> Type14 Atom))
(: typed-chain-choice (-> Type15 Atom))

(: typed-chain-value Type15)

(= (typed-chain-choice $value) $value)

(: typed-chain-loop (-[det]-> Number Atom))
(= (typed-chain-loop $n)
   (if (== $n 0)
       Done
       (let $value (typed-chain-choice typed-chain-value)
         (typed-chain-loop (- $n 1)))))

!(typed-chain-loop 20000)
