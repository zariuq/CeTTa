; Written lambda domains in the HOL profile: a proposition may write the
; domain of a quantified variable, the written domain is a contract checked
; against the quantifier's carrier, written and bare spellings are the same
; proposition, the native compiler keeps the declared domain, and a written
; domain that is another type former than the carrier is refuted.
!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: nat (u 0)) (: zero nat) (: succ (-> nat nat))))
!(pf:equality &thy nat refl@nat subst@nat)

; An axiom whose predicate writes its domains.
!(set:axiom &thy succ-inj (all nat (lam (x : nat) (all nat (lam (y : nat)
   (imp (eq nat (succ x) (succ y)) (eq nat x y)))))))
!(set:known-proposition &thy succ-inj)

; The written and the bare spelling are the same proposition, and a fact
; written one way proves a goal written the other way.
!(set:eq &thy (all nat (lam (x : nat) (eq nat x x))) (all nat (lam x (eq nat x x))))
!(set:axiom &thy refl-all (all nat (lam (x : nat) (eq nat x x))))
!(set:proves &thy (pf:fix z (pf:by refl-all)) (all nat (lam z (eq nat z z))))
!(pf:proves &thy (pf:by succ-inj (pf:refl nat (succ zero))) (eq nat zero zero))
!(set:proves &thy (pf:by refl-all) (eq nat zero zero))

; A theorem stated with a written domain, checked and compiled natively: the
; compiled term keeps the declared domain.
!(pf:theorem &thy refl-succ (all nat (lam (n : nat) (eq nat (succ n) (succ n))))
   (pf:fix n (pf:refl nat (succ n))))
!(set:known-proposition &thy refl-succ)
!(let $pkg (set:native-proof &thy refl-succ)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $pkg
     ($name $type)))

; A written domain that is another type former than the carrier is refuted.
!(set:axiom &thy bad-domain (all nat (lam (x : (-> nat nat)) (eq nat x x))))
!(set:check &thy (lam (x : (-> nat nat)) x) (-> nat nat))
; A written domain equal to the carrier checks.
!(set:check &thy (lam (x : nat) x) (-> nat nat))
