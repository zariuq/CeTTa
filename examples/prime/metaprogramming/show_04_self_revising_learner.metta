(set:profile hol)

; Why it matters: a learner that generalizes must be able to change its mind,
; and every conclusion drawn from a rule it has since revised must stop
; counting until it is derived again, while nothing unrelated is disturbed.
;
; The learner generalizes from observed animals to their kind: a kind flies
; unless one of its observed members was seen grounded.  It publishes the rule
; with set:define and its conclusions as checked theorems, including one about
; robin, which it never observed.  When a penguin is seen grounded it revises
; the rule.  The kernel then reports each conclusion that relied on the old
; rule as changed, also through the unchanged alias can-fly, and keeps an
; unrelated fact.  The learner renews the alias and derives again: robin still
; flies, pingu's old conclusion is withdrawn, and its negation is published.

!(bind! &kb (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &kb kind (u 0) (: songbird kind) (: penguin kind))
!(set:inductive &kb animal (u 0) (: tweety animal) (: pingu animal) (: robin animal))
!(set:define &kb falsum prop (= falsum (all prop (lam p p))))
!(set:define &kb verum prop (= verum (imp falsum falsum)))
!(set:define &kb kind-of (-> animal kind)
   (= (kind-of tweety) songbird) (= (kind-of pingu) penguin) (= (kind-of robin) songbird))

; ---- learning the rule from observations (the kernel answers kind queries)
(= (member-grounded $kind $grounded)
   (if (== $grounded ()) False
       (let ($a $rest) (decons-atom $grounded)
         (if (set:eq &kb (kind-of $a) $kind) True (member-grounded $kind $rest))){$a $rest}))
(= (kind-value $kind $grounded) (if (member-grounded $kind $grounded) falsum verum))
(= (publish-rule $grounded)
   (let $s (kind-value songbird $grounded)
     (let $p (kind-value penguin $grounded)
       (let $defined (set:define &kb flies-kind (-> kind prop)
                       (= (flies-kind songbird) $s) (= (flies-kind penguin) $p))
         (Rule (songbird $s) (penguin $p))))))

; ---- conclusions: whichever of "flies" and "grounded" the kernel accepts.
; A judgment reads its operands as written, so the animal is forced first.
(= (flies-name tweety) tweety-flies)
(= (flies-name pingu) pingu-flies)
(= (flies-name robin) robin-flies)
(= (grounded-name tweety) tweety-grounded)
(= (grounded-name pingu) pingu-grounded)
(= (grounded-name robin) robin-grounded)
(= (conclude $animal)
   (let $x $animal
     (if (set:proves &kb (pf:imp-intro (pf:hyp 0)) (can-fly $x))
         (let $n (flies-name $x)
           (let $r (set:theorem &kb $n (can-fly $x) (pf:imp-intro (pf:hyp 0)))
             (let $b (add-atom &self (believed $n (can-fly $x))) (Concluded $n)){$b}){$r}){$n}
         (let $n (grounded-name $x)
           (let $r (set:theorem &kb $n (imp (can-fly $x) falsum) (pf:imp-intro (pf:hyp 0)))
             (let $b (add-atom &self (believed $n (imp (can-fly $x) falsum)))
               (Concluded $n)){$b}){$r}){$n})))

; ---- the kernel's view of a published fact
(= (status $name $prop)
   (let $r (try (set:proves &kb (pf:known $name) $prop))
     (if (== (car-atom $r) Established) (Status $name holds) (Status $name $r))))

; ---- revision: retract a published definition or fact, then republish
(= (retract $name)
   (let $a (collapse (match &kb (set:known $name $p $o $proof $deps $r $s)
                        (remove-atom &kb (set:known $name $p $o $proof $deps $r $s))){$name})
     (let $b (collapse (match &kb (type:rule $name $n $pats $rhs)
                          (remove-atom &kb (type:rule $name $n $pats $rhs))){$name})
       (let $c (collapse (match &kb (: $name $ty) (remove-atom &kb (: $name $ty))){$name})
         (Retracted $name)))))
(= (derive-again)
   (let $old (collapse (match &self (believed $n $p)
                          (let $r (retract $n) (let $d (remove-atom &self (believed $n $p)) $n))))
     (let $new (collapse (conclude (superpose (tweety pingu robin))))
       (Rederived (Withdrawn $old) $new))))

; Batch 1: every observed animal flew.
!(publish-rule ())
!(set:define &kb can-fly (-> animal prop) (= (can-fly $x) (flies-kind (kind-of $x))))
!(set:theorem &kb verum-holds verum (pf:imp-intro (pf:hyp 0)))
!(collapse (conclude (superpose (tweety pingu robin))))

; Batch 2: pingu is seen grounded.  The learner revises its own rule.
!(retract flies-kind)
!(publish-rule (pingu))
; Every conclusion that relied on the old rule is reported as changed; the
; unrelated fact still holds.
!(status tweety-flies (can-fly tweety))
!(status pingu-flies (can-fly pingu))
!(status robin-flies (can-fly robin))
!(status verum-holds verum)
; The alias's own equation did not change, yet it was checked against the old
; body, so it must be renewed too.
!(try (set:recheck &kb can-fly))
!(retract can-fly)
!(set:define &kb can-fly (-> animal prop) (= (can-fly $x) (flies-kind (kind-of $x))))
!(try (set:recheck &kb can-fly))

; Derive again under the revised rule.
!(derive-again)
!(status tweety-flies (can-fly tweety))
!(status robin-flies (can-fly robin))
!(status pingu-grounded (imp (can-fly pingu) falsum))
!(status pingu-flies (can-fly pingu))
