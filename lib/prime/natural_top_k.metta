; A natural-score selector defined once in an admitted mathematical space.
; Rows retain source index, score and payload. Stable insertion and truncation
; compute the result; dependent proof programs certify its size bound.
(hol:profile base)
!(hol:inductive &self Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))
!(hol:inductive &self B (u 0) (: tt B) (: ff B))
!(hol:inductive &self Payload (u 0) (: pa Payload) (: pb Payload) (: pc Payload) (: pd Payload))
!(hol:inductive &self Row (u 0) (: row (-> Nat Nat Payload Row)))
!(hol:inductive &self Rows (u 0) (: RNil Rows) (: RCons (-> Row Rows Rows)))

!(hol:define &self geq (-> Nat Nat B)
  (= (geq $m Z) tt)
  (= (geq Z (S $n)) ff)
  (= (geq (S $m) (S $n)) (geq $m $n)))

!(hol:define &self leq (-> Nat Nat B)
  (= (leq Z $n) tt)
  (= (leq (S $m) Z) ff)
  (= (leq (S $m) (S $n)) (leq $m $n)))

!(hol:define &self lt (-> Nat Nat B)
  (= (lt $m Z) ff)
  (= (lt Z (S $n)) tt)
  (= (lt (S $m) (S $n)) (lt $m $n)))

!(hol:define &self pick (-> B Rows Rows Rows)
  (= (pick tt $a $b) $a)
  (= (pick ff $a $b) $b))

!(hol:define &self score (-> Row Nat)
  (= (score (row $i $s $p)) $s))

!(hol:define &self index (-> Row Nat)
  (= (index (row $i $s $p)) $i))

!(hol:define &self len (-> Rows Nat)
  (= (len RNil) Z)
  (= (len (RCons $r $rs)) (S (len $rs))))

!(hol:define &self insert (-> Row Rows Rows)
  (= (insert $r RNil) (RCons $r RNil))
  (= (insert $r (RCons $f $rest)) (pick (geq (score $r) (score $f)) (RCons $r (RCons $f $rest)) (RCons $f (insert $r $rest)))))

!(hol:define &self take (-> Nat Rows Rows)
  (= (take Z $rs) RNil)
  (= (take (S $k) RNil) RNil)
  (= (take (S $k) (RCons $r $rs)) (RCons $r (take $k $rs))))

!(hol:define &self bounded (-> Nat Rows Rows)
  (= (bounded $k RNil) RNil)
  (= (bounded $k (RCons $r $rs)) (take $k (insert $r (bounded $k $rs)))))

!(hol:define &self eligible (-> Nat Rows Rows)
  (= (eligible $b RNil) RNil)
  (= (eligible $b (RCons $r $rs)) (pick (lt (index $r) $b) (RCons $r (eligible $b $rs)) (eligible $b $rs))))

!(hol:define &self topk (-> Nat Nat Rows Rows)
  (= (topk $before $budget $rows) (bounded $budget (eligible $before $rows))))

!(hol:define &self take-bound (-> (k : Nat) (rs : Rows) (id B (leq (len (take k rs)) k) tt))
  (= (take-bound Z $rs) (refl tt))
  (= (take-bound (S $k) RNil) (refl tt))
  (= (take-bound (S $k) (RCons $r $rs)) (take-bound $k $rs)))

!(hol:define &self bounded-bound (-> (k : Nat) (rs : Rows) (id B (leq (len (bounded k rs)) k) tt))
  (= (bounded-bound $k RNil) (refl tt))
  (= (bounded-bound $k (RCons $r $rs)) (take-bound $k (insert $r (bounded $k $rs)))))


; The bound proof is an ordinary declared function. Its result type mentions
; the same selector that runs below, for arbitrary inputs and budgets.
!(hol:define &self topk-bound
  (-> (before : Nat) (budget : Nat) (rows : Rows)
      (id B (leq (len (topk before budget rows)) budget) tt))
  (= (topk-bound $before $budget $rows)
     (bounded-bound $budget (eligible $before $rows))))
