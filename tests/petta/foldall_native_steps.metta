; foldall folds a step over its generator's answers, each answer and the
; state so far giving the next state, as SWI's foldall/4 does; a step
; without an answer keeps the state.  With a step that is a deterministic
; function of values, the machine collects the answers and folds them
; itself, and a sum of one literal integer over a match's answers is that
; integer times their number (SWI-PeTTa's answers).

(num 3) (num 1) (num 4)
(item p) (item q)
(dup a) (dup a)
(= (gen3) (superpose (1 2 3)))

!(foldall + (superpose (1 2 3)) 0)
!(foldall + (match &self (num $x) $x) 0)
!(foldall - (superpose (1 2 3)) 10)
!(foldall * (gen3) 1)
!(foldall + (superpose ()) 5)
!(foldall union-atom (superpose ((a) (b))) ())
!(foldall union-atom (superpose (a (b) (c d))) ())
!(msort (foldall union-atom (match &self (item $x) ($x)) ()))
!(foldall union-atom (superpose ((1 2) () (3))) ())
!(foldall max (superpose (3 1 4)) 0)
!(foldall + (let $u (add-atom &self (seen 1)) 1) 0)
!(collapse (match &self (seen $x) $x))
!(collapse (foldall + (superpose (1 a 2)) 0))
!(foldall union-atom (superpose ((cons 1 (cons 2 ())) (3))) ())
!(foldall union-atom (superpose ((cons 1 foo) (3))) ())
!(foldall + (match &self (num $x) 1) 0)
!(foldall + (match &self (num $x) 2) 10)
!(foldall + (match &self (none $x) 1) 5)
!(foldall + (match &self (dup $x) 1) 0)
