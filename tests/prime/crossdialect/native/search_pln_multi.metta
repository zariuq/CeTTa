; Native dependent PLN evidence search, using exact positive/total counts.
; The four inference constructors are explicit hypotheses of this theory.
; This tests recursive evidence aggregation and retained search prefixes;
; it does not import the HE floating-point truth-value evaluator.
!(bind! &thy (new-space))
!(set:inductive &thy Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))
!(set:inductive &thy Bool (u 0) (: yes Bool) (: no Bool))
!(set:inductive &thy Evidence (u 0) (: ENil Evidence) (: ECons (-> Nat Evidence Evidence)))
!(set:define &thy flagCount (-> Bool Nat) (= (flagCount yes) (S Z)) (= (flagCount no) Z))
!(set:define &thy addEvidence (-> Bool Nat Nat) (= (addEvidence yes $pos) (S $pos)) (= (addEvidence no $pos) $pos))
!(add-atom &thy (: Event (u 0)))
!(add-atom &thy (: P Event))
!(add-atom &thy (: Q Event))
!(add-atom &thy (: Seen (-> (p : Event) (i : Nat) (b : Bool) (u 0))))
!(add-atom &thy (: Inferred (-> (p q : Event) (es : Evidence) (positive total : Nat) (u 0))))
!(add-atom &thy (: Earlier (-> Nat Nat (u 0))))
!(add-atom &thy (: p0 (Seen P Z yes)))
!(add-atom &thy (: q0 (Seen Q Z no)))
!(add-atom &thy (: p1 (Seen P (S Z) yes)))
!(add-atom &thy (: q1 (Seen Q (S Z) yes)))
!(add-atom &thy (: order01 (Earlier Z (S Z))))
!(add-atom &thy (: baseNo
   (-> (p q : Event) (i : Nat)
       (hp : (Seen p i yes)) (hq : (Seen q i no))
       (Inferred p q (ECons i ENil) Z (S Z)))))
!(add-atom &thy (chaining-rule baseNo))
!(add-atom &thy (: baseYes
   (-> (p q : Event) (i : Nat)
       (hp : (Seen p i yes)) (hq : (Seen q i yes))
       (Inferred p q (ECons i ENil) (S Z) (S Z)))))
!(add-atom &thy (chaining-rule baseYes))
!(add-atom &thy (: extendYes
   (-> (p q : Event) (i h : Nat) (tail : Evidence)
       (positive total : Nat)
       (hp : (Seen p i yes)) (hq : (Seen q i yes))
       (prior : (Inferred p q (ECons h tail) positive total))
       (order : (Earlier h i))
       (Inferred p q (ECons i (ECons h tail)) (S positive) (S total)))))
!(add-atom &thy (chaining-rule extendYes))
!(add-atom &thy (: extendNo
   (-> (p q : Event) (i h : Nat) (tail : Evidence)
       (positive total : Nat)
       (hp : (Seen p i yes)) (hq : (Seen q i no))
       (prior : (Inferred p q (ECons h tail) positive total))
       (order : (Earlier h i))
       (Inferred p q (ECons i (ECons h tail)) positive (S total)))))
!(add-atom &thy (chaining-rule extendNo))

; Every returned proof is checked afresh, including after the search budget
; runs out. Distinct proof histories remain visible in the first Boolean.
(= (observe-evidence (typed-answer-v2 (: $proof $type) $substitution))
   (checked
     (=alpha $proof
       (extendYes P Q (S Z) Z ENil Z (S Z) p1 q1
         (baseNo P Q Z p0 q0) order01))
     (type:eq &thy $proof
       (extendYes P Q (S Z) Z ENil Z (S Z) p1 q1
         (baseNo P Q Z p0 q0) order01))
     (type:check &thy $proof
       (Inferred P Q (ECons (S Z) (ECons Z ENil)) (S Z) (S (S Z))))
     (type:check &thy $proof
       (Inferred P Q (ECons (S Z) (ECons Z ENil)) Z (S (S Z))))))
(= (observe-search
     (typing-search $status $depth $reason $proofs $stats))
   (let $answers (cdr-atom $proofs)
     (search $status $reason
       (collapse (let $answer (superpose $answers) (observe-evidence $answer))))))

!(type:eq &thy (flagCount no) Z)
!(type:eq &thy (addEvidence yes Z) (S Z))
!(observe-search
   (search-inhabitants &thy
     (Inferred P Q (ECons (S Z) (ECons Z ENil)) (S Z) (S (S Z)))
     3 200000 20))

; The admitted identity gives arbitrarily many evidence histories. A larger
; exploration must retain checked answers when its finite budget runs out.
!(set:define &thy retain
   (-> (p q : Event) (es : Evidence) (positive total : Nat)
       (w : (Inferred p q es positive total))
       (Inferred p q es positive total))
   (= (retain $p $q $es $positive $total $w) $w))
!(add-atom &thy (chaining-rule retain))

!(observe-search
   (search-inhabitants &thy
     (Inferred P Q (ECons (S Z) (ECons Z ENil)) (S Z) (S (S Z)))
     8 500000 20))
; The direct recursive proof also fails when its observations are swapped.
!(type:check &thy
   (extendYes P Q (S Z) Z ENil Z (S Z) q1 p1
     (baseNo P Q Z p0 q0) order01)
   (Inferred P Q (ECons (S Z) (ECons Z ENil)) (S Z) (S (S Z))))
