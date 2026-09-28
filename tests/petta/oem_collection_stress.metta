; Answers the open-equation machine gives across its collections: choice
; frames that outlive many collections, cells bound and slots stored after
; the activations that own them grew old, backtracking into frames made
; before a collection, and space effects inside the loop.  The gate runs it
; with collections forced every few kilobytes and compares with SWI-PeTTa.

(Evaluation (philosopher Plato))
(Evaluation (likes-to-wrestle Plato))
(Implication
   (And (Evaluation (philosopher $x)) (Evaluation (likes-to-wrestle $x)))
   (Evaluation (human $x)))
(Implication (Evaluation (human $x)) (Evaluation (mortal $x)))

(= (deduce (Evaluation ($predicate $x)))
   (match &self (Evaluation ($predicate $x)) T))
(= (deduce (Evaluation ($predicate $x)))
   (match &self (Implication $antecedent (Evaluation ($predicate $x)))
     (deduce $antecedent)))
(= (deduce (And $left $right))
   (both (deduce $left) (deduce $right)))
(= (both T T) T)

(= (once-more $iteration)
   (let $subject (if (== (* 0 $iteration) 0) Plato Not-Plato)
     (if (== (deduce (Evaluation (mortal $subject))) T) 1 0)))

(= (deduction-loop $remaining $sum)
   (if (< $remaining 1)
       $sum
       (let $next (- $remaining 1)
         (let $value (once-more $next)
           (deduction-loop $next (+ $sum $value))))))

!(deduction-loop 400 0)

; Nondeterminism whose alternatives are explored after many collections.
(= (pick $n) (if (< $n 1) (empty) (superpose ($n (pick (- $n 1))))))
!(size-atom (collapse (pick 300)))

; A space effect inside a nondeterministic loop.
(= (record $n)
   (if (< $n 1)
       done
       (let $unused (add-atom &self (seen $n))
         (record (- $n 1)))))
!(record 200)
!(size-atom (collapse (match &self (seen $k) $k)))

; Temporaries that die after collections promote them: `once` drops each
; iteration's alternatives, so major collections have old garbage to reclaim.
(= (junk $n) (if (< $n 1) () (cons-atom $n (junk (- $n 1)))))
(= (walk $n $acc)
   (if (< $n 1)
       $acc
       (let $x (once (pick 5))
         (let $list (junk 20)
           (walk (- $n 1) (+ $acc (+ $x (size-atom $list))))))))
!(walk 12000 0)

; A chain of last calls whose caller stores the value it gets back: the
; caller's slot takes the cell the chain fills, after work that made the
; caller's slots old, and collections run while the chain does, each
; reaching the caller's slots both through its continuation and through the
; slot the chain stored.
(= (best-of $best ()) $best)
(= (best-of $best (Cons $x $rest))
   (if (> $x $best) (best-of $x $rest) (best-of $best $rest)))
(= (length-of ()) 0)
(= (length-of (Cons $x $rest)) (+ 1 (length-of $rest)))
(= (remove-one $m ()) ())
(= (remove-one $m (Cons $x $rest))
   (if (== $m $x) $rest (Cons $x (remove-one $m $rest))))
(= (trim-to $l $n)
   (if (<= (length-of $l) $n)
       $l
       (let $m (best-of 0 $l) (trim-to (remove-one $m $l) $n))))
(= (numbers $n) (if (== $n 0) () (Cons $n (numbers (- $n 1)))))
(= (open-pair) (Two $u $v))
(= (churn $n) (if (== $n 0) 0 (let (Two $p $q) (open-pair) (churn (- $n 1)))))
(= (busy $n) (if (== $n 0) 0 (+ 1 (busy (- $n 1)))))
(= (trimmed $l $n $k)
   (let $w (busy 3000)
     (let $g (churn $k)
       (let $s (trim-to $l $n)
         (Pair $s (length-of $s))))))
!(trimmed (numbers 300) 4 7)
!(trimmed (numbers 600) 4 40)
