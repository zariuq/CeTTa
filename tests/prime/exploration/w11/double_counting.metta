(set:profile hol)

; Forgetting provenance double-counts evidence.  A derivation records the
; answer it supports and the source it rests on.  In the world shared, one
; source (7) supports the answer through two derivations, as when one report
; is read twice.  In the world independent, two sources (7 and 8) support it
; once each.  A later decision accepts an answer with at least two pieces of
; evidence.  Values are bound with let before a list builtin reads them: a
; builtin that takes an atom reads an expression written in its argument as
; syntax.

; ---- Part 1: the program -----------------------------------------------------

(= (derivation-of shared) (derivation True 7))
(= (derivation-of shared) (derivation True 7))
(= (derivation-of independent) (derivation True 7))
(= (derivation-of independent) (derivation True 8))
(= (derivations $world) (collapse (derivation-of $world)))

; Forgetting provenance keeps the bag of answers.
(= (answers $ds) (map-atom $ds $d (unify $d (derivation $a $s) $a no-answer)))

; Evidence for a target, counted two ways: the derivations in the bag of
; answers, or the distinct sources of the derivations.
(= (derivation-count $answers $target)
   (foldl-atom $answers 0 $n $a (if (== $a $target) (+ $n 1) $n)))
(= (sources-for $ds $target)
   (unique (let $d (superpose $ds) (unify $d (derivation $target $s) $s (empty)))))
(= (source-count $ds $target)
   (let $sources (collapse (sources-for $ds $target)) (size-atom $sources)))

(= (accept $evidence) (<= 2 $evidence))
(= (naive-decision $answers) (accept (derivation-count $answers True)))
(= (aware-decision $ds) (accept (source-count $ds True)))

; 1. The two worlds have one bag of answers (double_counting) ...
!(let* (($s (derivations shared)) ($i (derivations independent)))
   (== (answers $s) (answers $i)))
; 2. ... counting derivations accepts the shared world ...
!(let $s (derivations shared) (naive-decision (answers $s)))
; 3. ... counting sources rejects it ...
!(let $s (derivations shared) (aware-decision $s))
; 4. ... and accepts the independent one (double_counting).
!(let $i (derivations independent) (aware-decision $i))
; 5. So the two decisions differ (naive_ne_aware) ...
!(let $s (derivations shared) (== (naive-decision (answers $s)) (aware-decision $s)))
; 6. ... and the source-aware decision cannot be computed from the bag of
;    answers: one bag, two decisions (decision_not_descends).
!(let* (($s (derivations shared)) ($i (derivations independent)))
   (and (== (answers $s) (answers $i))
        (not (== (aware-decision $s) (aware-decision $i)))))

; ---- Part 2: the same facts, proved by computation ---------------------------

; The evidence as data in the checker's theory: lists of derivations, the
; answers they support, and the decisions, each defined by recursion on one
; argument.  Each fact is proved by reflexivity: the checker computes both
; sides and compares them.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &thy bool (u 0) (: true bool) (: false bool))
!(set:inductive &thy nat (u 0) (: O nat) (: S (-> nat nat)))
!(set:inductive &thy deriv (u 0) (: mk-deriv (-> bool nat deriv)))
!(set:inductive &thy dlist (u 0) (: dnil dlist) (: dcons (-> deriv dlist dlist)))
!(set:inductive &thy blist (u 0) (: bnil blist) (: bcons (-> bool blist blist)))
!(set:inductive &thy nlist (u 0) (: nnil nlist) (: ncons (-> nat nlist nlist)))
!(pf:equality &thy bool refl@bool subst@bool)
!(pf:equality &thy blist refl@blist subst@blist)
!(set:define &thy andb (-> bool bool bool) (= (andb true $y) $y) (= (andb false $y) false))
!(set:define &thy orb (-> bool bool bool) (= (orb true $y) true) (= (orb false $y) $y))
!(set:define &thy notb (-> bool bool) (= (notb true) false) (= (notb false) true))
!(set:define &thy is-zero (-> nat bool) (= (is-zero O) true) (= (is-zero (S $n)) false))
!(set:define &thy pred (-> nat nat) (= (pred O) O) (= (pred (S $n)) $n))
!(set:define &thy if-zero (-> nat nat nat nat) (= (if-zero O $z $s) $z) (= (if-zero (S $n) $z $s) $s))
; n - m, cut off at zero
!(set:define &thy monus (-> nat nat nat)
   (= (monus O $n) $n) (= (monus (S $m) $n) (if-zero $n O (monus $m (pred $n)))))
!(set:define &thy nat-eqb (-> nat nat bool)
   (= (nat-eqb $n $m) (andb (is-zero (monus $n $m)) (is-zero (monus $m $n)))))
; a derivation's answer and source
!(set:define &thy answer-of (-> deriv bool) (= (answer-of (mk-deriv $a $s)) $a))
!(set:define &thy source-of (-> deriv nat) (= (source-of (mk-deriv $a $s)) $s))
; forgetting provenance: the bag of answers
!(set:define &thy answers-of (-> dlist blist)
   (= (answers-of dnil) bnil)
   (= (answers-of (dcons $d $rest)) (bcons (answer-of $d) (answers-of $rest))))
; counting derivations of true
!(set:define &thy add-if (-> bool nat nat) (= (add-if true $n) (S $n)) (= (add-if false $n) $n))
!(set:define &thy count-true (-> blist nat)
   (= (count-true bnil) O)
   (= (count-true (bcons $b $rest)) (add-if $b (count-true $rest))))
; counting distinct sources of true
!(set:define &thy cons-if (-> bool nat nlist nlist)
   (= (cons-if true $n $l) (ncons $n $l)) (= (cons-if false $n $l) $l))
!(set:define &thy true-sources (-> dlist nlist)
   (= (true-sources dnil) nnil)
   (= (true-sources (dcons $d $rest))
      (cons-if (answer-of $d) (source-of $d) (true-sources $rest))))
!(set:define &thy mem (-> nlist nat bool)
   (= (mem nnil $n) false) (= (mem (ncons $m $l) $n) (orb (nat-eqb $n $m) (mem $l $n))))
!(set:define &thy dedup (-> nlist nlist)
   (= (dedup nnil) nnil)
   (= (dedup (ncons $n $l)) (cons-if (notb (mem $l $n)) $n (dedup $l))))
!(set:define &thy len (-> nlist nat) (= (len nnil) O) (= (len (ncons $n $l)) (S (len $l))))
; the decision: at least two pieces of evidence
!(set:define &thy is-succ (-> nat bool) (= (is-succ O) false) (= (is-succ (S $n)) true))
!(set:define &thy at-least-two (-> nat bool) (= (at-least-two $n) (is-succ (pred $n))))
!(set:define &thy by-derivations (-> blist bool)
   (= (by-derivations $as) (at-least-two (count-true $as))))
!(set:define &thy by-sources (-> dlist bool)
   (= (by-sources $ds) (at-least-two (len (dedup (true-sources $ds))))))
; the two worlds, with sources 7 and 8
!(set:define &thy seven nat (= seven (S (S (S (S (S (S (S O)))))))))
!(set:define &thy eight nat (= eight (S seven)))
!(set:define &thy shared-evidence dlist
   (= shared-evidence (dcons (mk-deriv true seven) (dcons (mk-deriv true seven) dnil))))
!(set:define &thy independent-evidence dlist
   (= independent-evidence (dcons (mk-deriv true seven) (dcons (mk-deriv true eight) dnil))))

; 7. The four facts of double_counting: one bag of answers, the shared world
;    accepted by counting derivations, rejected by counting sources, and the
;    independent world accepted by counting sources.
!(pf:theorem &thy one-bag
   (eq blist (answers-of shared-evidence) (answers-of independent-evidence))
   (pf:refl blist (bcons true (bcons true bnil))))
!(pf:theorem &thy shared-accepted-by-derivations
   (eq bool (by-derivations (answers-of shared-evidence)) true)
   (pf:refl bool true))
!(pf:theorem &thy shared-rejected-by-sources
   (eq bool (by-sources shared-evidence) false)
   (pf:refl bool false))
!(pf:theorem &thy independent-accepted-by-sources
   (eq bool (by-sources independent-evidence) true)
   (pf:refl bool true))
; 8. The opposite of the third fact is refused (double_counting).
!(pf:theorem &thy shared-accepted-by-sources
   (eq bool (by-sources shared-evidence) true)
   (pf:refl bool true))
