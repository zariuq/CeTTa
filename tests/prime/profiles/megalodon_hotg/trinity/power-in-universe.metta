(set:profile megalodon-hotg)

; The power set of a set lies in its universe: for every set N,
;   Power N ∈ UnivOf N.
; Two laws of the signature give it.  universeIn says N ∈ UnivOf N, and
; universeClosed says that UnivOf N is closed under Union, Power and Repl,
; three conditions in one conjunction (conjunction is written out from imp
; and all: P and Q is "every r that follows from P and Q holds").  The power
; set of N is in the universe because N is and the universe is closed under
; Power.  Nothing here runs: the statement is about sets, and its proofs are
; data.
!(import! &self pf)

; ---- Extensional: the theorem, from universeIn and universeClosed.
; The closure at N is taken apart twice: its second component, and of that
; the first, the closure under Power; at N it gives the theorem from N ∈ UnivOf N.
!(pf:theorem &self power-in-universe (all set (lam N (In (Power N) (UnivOf N))))
   (pf:fix N
     (pf:have closed (pf:all-elim (pf:known universeClosed) N)
       (pf:have power-closed
         (all set (lam x (imp (In x (UnivOf N)) (In (Power x) (UnivOf N)))))
         (pf:by closed (pf:assume union-closed (pf:assume rest
           (pf:by rest (pf:assume power (pf:assume replacement power))))))
         (pf:by power-closed (pf:all-elim (pf:known universeIn) N))))))
!(set:known-proposition &self power-in-universe)
; Used at Empty, and at the universe of Empty, which is a set too.
!(pf:proves &self (pf:by power-in-universe) (In (Power Empty) (UnivOf Empty)))
!(pf:proves &self (pf:by power-in-universe)
   (In (Power (UnivOf Empty)) (UnivOf (UnivOf Empty))))
; universeIn alone does not prove it: N ∈ UnivOf N is not Power N ∈ UnivOf N.
!(pf:proves &self (pf:fix N (pf:all-elim (pf:known universeIn) N))
   (all set (lam N (In (Power N) (UnivOf N)))))
; The converse, every universe is a member of the power set, is not proved by
; the same steps,
!(pf:proves &self
   (pf:fix N
     (pf:have closed (pf:all-elim (pf:known universeClosed) N)
       (pf:have power-closed
         (all set (lam x (imp (In x (UnivOf N)) (In (Power x) (UnivOf N)))))
         (pf:by closed (pf:assume union-closed (pf:assume rest
           (pf:by rest (pf:assume power (pf:assume replacement power))))))
         (pf:by power-closed (pf:all-elim (pf:known universeIn) N)))))
   (all set (lam N (In (UnivOf N) (Power N)))))
; and the laws refute it at N = Empty.  If UnivOf Empty were a member of
; Power Empty, the power law would make every member of UnivOf Empty a member
; of Empty; Empty is one (universeIn), and nothing is a member of Empty
; (emptyLaw).
!(pf:theorem &self universe-in-power-refuted
   (imp (all set (lam N (In (UnivOf N) (Power N)))) Falsum)
   (pf:assume H
     (pf:have power (pf:all-elim (pf:all-elim (pf:known powerLaw) Empty) (UnivOf Empty))
       (pf:have subset (all set (lam y (imp (In y (UnivOf Empty)) (In y Empty))))
         (pf:by power (pf:assume forward (pf:assume backward
           (pf:imp-elim forward (pf:all-elim H Empty)))))
         (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty)
           (pf:by subset (pf:all-elim (pf:known universeIn) Empty)))))))
; The same argument does not refute the theorem itself: Power Empty ∈
; UnivOf Empty gives nothing about the members of Empty.
!(pf:proves &self
   (pf:assume H
     (pf:have power (pf:all-elim (pf:all-elim (pf:known powerLaw) Empty) (UnivOf Empty))
       (pf:have subset (all set (lam y (imp (In y (UnivOf Empty)) (In y Empty))))
         (pf:by power (pf:assume forward (pf:assume backward
           (pf:imp-elim forward (pf:all-elim H Empty)))))
         (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty)
           (pf:by subset (pf:all-elim (pf:known universeIn) Empty))))))
   (imp (all set (lam N (In (Power N) (UnivOf N)))) Falsum))

; ---- Intensional: the theorem as a typed proof term.
; The same proof by the rules alone, with each conjunction taken apart by
; instantiating it at the component wanted.  The native compiler turns it
; into a term of the kernel: the quantifier over all sets by the rule
; constants allI@set and allE@set, the two laws as its assumptions.
!(set:theorem power-in-universe-term (all set (lam N (In (Power N) (UnivOf N))))
   (pf:all-intro
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim
             (pf:imp-elim
               (pf:all-elim (pf:all-elim (pf:known universeClosed) (pf:var 0))
                 (all prop (lam r
                   (imp (imp (all set (lam x (imp (In x (UnivOf (pf:var 0)))
                                                  (In (Power x) (UnivOf (pf:var 0))))))
                             (imp (all set (lam a
                                    (imp (In a (UnivOf (pf:var 0)))
                                         (all (-> set set) (lam f
                                           (imp (all set (lam x (imp (In x a)
                                                                     (In (f x) (UnivOf (pf:var 0))))))
                                                (In (Repl a f) (UnivOf (pf:var 0)))))))))
                                  r))
                        r))))
               (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
             (all set (lam x (imp (In x (UnivOf (pf:var 0))) (In (Power x) (UnivOf (pf:var 0)))))))
           (pf:imp-intro (pf:imp-intro (pf:hyp 1))))
         (pf:var 0))
       (pf:all-elim (pf:known universeIn) (pf:var 0)))))
; Its term introduces the quantifier by allI@set, and its assumptions are the
; two laws.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof power-in-universe-term)
   (let (App (App (DeclConst $intro) $predicate) $body) $term
     (let (($first $first-law $first-type) ($second $second-law $second-type)) $assumptions
       (TypedProof $name $intro ($first $second)))))
; Its type is checked: the term is used at Empty by allE@set, and the result
; has the type of the statement at Empty.
!(let $package (set:native-proof power-in-universe-term)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let (SetNativeUseV1 $checked $consumer $result $result-type)
            (set:native-use $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (DeclConst Empty))))
         (UsedAt (== $checked $package)
           (== $result-type
             (App (DeclConst $family)
               (App (App (DeclConst In) (App (DeclConst Power) (DeclConst Empty)))
                    (App (DeclConst UnivOf) (DeclConst Empty))))))))))
; The readable proof above uses pf:have and pf:by, and has a package too.  A
; local lemma (pf:have) compiles to a redex, the lemma's proof given to a
; function of its name, and a pf:by step to the eliminations the checker
; found for it, the same rules of the logic the term above is built from.
!(let $r (try (set:native-proof power-in-universe)) (car-atom $r))

; ---- Operational: nothing to run.
; Power and UnivOf have no equation, so a set stays as written, and so does
; a proof: evaluating one fires no rule.  Checking a use of the theorem is a
; judgment, not a run of the proof.  Each count is printed with the term it
; counts: the set; the readable proof's use at Empty, a pf: term evaluated as
; data, not the kernel term of the rule constants; and the judgment that
; checks that use, whose evaluation fires no equation of a declared
; definition.  Neither is the count Lean states, of the steps of the type of
; the proofs.
!(let (quote ($values $n)) (lift (cost:firings (Power (UnivOf Empty))))
   (quote (firings-of (Power (UnivOf Empty)) $values $n)))
!(let (quote ($values $n)) (lift (cost:firings (pf:all-elim (pf:known power-in-universe) Empty)))
   (quote (firings-of (pf:all-elim (pf:known power-in-universe) Empty) $values $n)))
!(let (quote ($values $n)) (lift (cost:firings (set:proves (pf:all-elim (pf:known power-in-universe) Empty)
                                      (In (Power Empty) (UnivOf Empty)))))
   (quote (firings-of (set:proves (pf:all-elim (pf:known power-in-universe) Empty)
                                (In (Power Empty) (UnivOf Empty))) $values $n)))
