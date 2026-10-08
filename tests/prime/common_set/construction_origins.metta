; Constructor-shaped syntax is not evidence of free generation. Structural
; definitions require an actual datatype admission in the current profile.
(: T (u 0))
(: z T)
(: s (-> T T))
!(let $digest (set:signature-digest)
    (add-atom &self (set:known T
      (inductive (u 0) (: z T) (: s (-> T T))) inductive () () 0 $digest)))
!(car-atom (try (set:recheck T)))
!(car-atom (try (set:define forged-pred (-> T T)
    (= (forged-pred z) z) (= (forged-pred (s $x)) $x))))
!(== (collapse (match &self (set:known forged-pred $p $o $f $d $r $signature) $p){$p $o $f $d $r $signature}) ())
!(== (collapse (match &self (type:rule forged-pred $arity $patterns $body) $body){$arity $patterns $body}) ())
!(forged-pred (s z))

; Genuine constructors support the same structural clauses.
!(set:inductive Real (u 0) (: zero Real) (: succ (-> Real Real)))
!(car-atom (try (set:recheck Real)))
!(set:define real-pred (-> Real Real)
    (= (real-pred zero) zero) (= (real-pred (succ $x)) $x))
!(real-pred (succ zero))

; Originating admission alone is insufficient under another profile.
!(add-atom &self (set:profile foundation-common))
!(car-atom (try (set:define foreign-pred (-> Real Real)
    (= (foreign-pred zero) zero) (= (foreign-pred (succ $x)) $x))))
!(== (collapse (match &self (set:known foreign-pred $p $o $f $d $r $signature) $p){$p $o $f $d $r $signature}) ())
!(remove-atom &self (set:profile foundation-common))
!(set:define restored-pred (-> Real Real)
    (= (restored-pred zero) zero) (= (restored-pred (succ $x)) $x))
!(restored-pred (succ zero))
