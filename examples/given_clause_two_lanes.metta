; A given-clause loop with two lanes over one clause store.
;
; A clause waits in two priority spaces at once: by weight, as (W A C), the
; lightest first and the oldest among equals; and by age, as (A W C), the
; oldest first.  Most picks take the lightest clause; every fifth takes the
; oldest, so a clause is picked within five times its age however many
; lighter clauses keep arriving.  Picking a clause from one lane removes it
; from the other: each lane finds an entry by its order, so the removal
; costs a search, not a scan.
;
; &seen holds every clause ever offered, so a clause is offered once.
; &active holds the clauses picked.  A clause's consequences are computed
; when it is picked, against the active clauses only (the given-clause
; discipline of Otter).
;
; The run ends in one of three answers:
;   (proved N)      goal was picked at pick N
;   (saturated N)   nothing waits: every consequence is in &active
;   (incomplete N)  the budget ran out with clauses still waiting, which
;                   says nothing about what they would have derived

!(bind! &active (new-space set))
!(bind! &seen (new-space set))
!(bind! &by-weight (new-space priority))
!(bind! &by-age (new-space priority))

(= (weight (path $x $y)) 1)
(= (weight (nat $n)) 0)
(= (weight goal) 9)

; A clause enters both lanes with the next age, unless it was offered before.
(= (offer $c $age)
   (if (== (collapse (match &seen $c seen)) ())
       (let* (($w (weight $c))
              ($_1 (add-atom &seen $c))
              ($_2 (add-atom &by-weight ($w $age $c)))
              ($_3 (add-atom &by-age ($age $w $c))))
          (+ $age 1))
       $age))
(= (offer-all $cs $age)
   (if (== $cs ()) $age
       (let* ((($c $rest) (decons-atom $cs))
              ($next (offer $c $age)))
          (offer-all $rest $next))))

; The lightest clause, or on every FAIR-th pick the oldest (FAIR 0: never).
(= (oldest-turn $n $fair)
   (if (== $fair 0) False (== (% $n $fair) (- $fair 1))))
(= (pick $n $fair)
   (if (oldest-turn $n $fair)
       (let* ((($a $w $c) (space:priority:pop &by-age))
              ($_ (remove-atom &by-weight ($w $a $c))))
          $c)
       (let* ((($w $a $c) (space:priority:pop &by-weight))
              ($_ (remove-atom &by-age ($a $w $c))))
          $c)))

; What a picked clause derives with the clauses picked before it.
(= (consequences (path $x $y))
   (let* (($right (collapse (match &active (path $y $z) (path $x $z))))
          ($left (collapse (match &active (path $v $x) (path $v $y)))))
      (union-atom $right $left)))
(= (consequences (nat $n)) (let $m (+ $n 1) ((nat $m))))
(= (consequences goal) ())

(= (run $age $n $budget $fair)
   (if (== (space-len &by-weight) 0) (saturated $n)
   (if (== $budget 0) (incomplete $n)
       (let $given (pick $n $fair)
          (if (== $given goal) (proved $n)
              (let* (($_ (add-atom &active $given))
                     ($new (consequences $given))
                     ($next (offer-all $new $age)))
                 (run $next (+ $n 1) (- $budget 1) $fair)))))))

; A finite problem saturates: the paths of a chain of four edges.
!(chain (run (offer-all ((path a b) (path b c) (path c d) (path d e)) 0) 0 100 5))
!(paths (size-atom (collapse (match &active (path $x $y) ok))))

; A problem that never saturates: nat keeps deriving a lighter clause than
; goal.  Without the age lane goal waits for ever; with it goal is picked.
!(bind! &active (new-space set))
!(bind! &seen (new-space set))
!(bind! &by-weight (new-space priority))
!(bind! &by-age (new-space priority))
!(lightest-only (run (offer-all ((nat 0) goal) 0) 0 200 0))
!(bind! &active (new-space set))
!(bind! &seen (new-space set))
!(bind! &by-weight (new-space priority))
!(bind! &by-age (new-space priority))
!(with-age-lane (run (offer-all ((nat 0) goal) 0) 0 200 5))
