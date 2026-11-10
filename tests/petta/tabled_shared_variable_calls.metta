;; Tabled calls whose arguments share a variable.  A second-order prover
;; carries the metasubstitution it extends, so a fresh predicate variable
;; occurs both in the remaining goals and in the substitution passed on.
!(import! &self (library lib_tabling))
(mr identity $P (sub identity $P $Q) (m $P $x $y) ((m $Q $x $y)))
(m parent stathis kostas)
(= (app $xs $ys) (if (== $xs ()) $ys (let ($h $t) (decons-atom $xs) (let $r (app $t $ys) (cons-atom $h $r)))))
(= (mem $xs) (if (== $xs ()) (empty) (let ($h $t) (decons-atom $xs) $h)))
(= (mem $xs) (if (== $xs ()) (empty) (let ($h $t) (decons-atom $xs) (mem $t))))
!(tabled (tp $goals $subs $k))
(= (tp $goals $subs $k)
   (if (== $goals ()) $subs (let ($g $rest) (decons-atom $goals) (tr $g $rest $subs $k))))
(= (tr $g $rest $subs $k) (match &self $g (tp $rest $subs $k)))
(= (tr $g $rest $subs $k)
   (let $s (mem $subs) (match &self (mr $id $P $s $g $body) (let $gs (app $body $rest) (tp $gs $subs $k)))))
(= (tr $g $rest $subs $k)
   (if (< (size-atom $subs) $k)
       (match &self (mr identity $P $s $g $body)
         (let $P ancestor (let $gs (app $body $rest) (tp $gs (cons-atom $s $subs) $k))))
       (empty)))
;; Positive: one invented clause proves the example.
!(collapse (tp ((m ancestor stathis kostas)) () 1))
;; Negative: no clause may be added, so nothing proves it.
!(collapse (tp ((m ancestor stathis kostas)) () 0))
;; Negative: no background fact supports the example.
!(collapse (tp ((m ancestor kostas stathis)) () 1))

;; The same variable in two argument positions of one tabled call.
!(tabled (pair-of $x $y))
(= (pair-of $x $x) same)
(= (pair-of a b) different)
!(collapse (let $v (pair-of $z $z) $v))
!(collapse (pair-of k k))
;; Negative: distinct values that no equation pairs.
!(collapse (pair-of b a))
!(collapse (pair-of a b))
