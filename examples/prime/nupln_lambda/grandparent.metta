; Learning grandparent with lambda hypotheses.
;
; The hypothesis has two object variables, x and z, and a hidden one, y,
; bound by an existential: (lam x (lam z (exists Person (lam y ...)))).
; Every binder is a lambda, so the language keeps the scopes apart; the
; learner's unknowns are the metavariables of the rules that generate the
; hypothesis.  The same rules that learned Nil's conjunction learn this.
;
; Part 2 uses the data of the meta-interpretive grandparent examples.  With
; one positive and one negative example the smallest consistent hypothesis
; is not grandparent; a chain metarule, written as a lambda whose predicate
; positions are metavariables, restores the intended bias.

!(import! &self nupln_lambda_core.metta)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Part 1: a family, parent evidence only ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

!(bind! &kb (new-space))
!(add-atom &kb (nu:domain Person))
!(add-atom &kb (nu:bias ∧))
!(add-atom &kb (nu:bias exists))
!(add-atom &kb (: parent (-> Person (-> Person Bool))))
!(add-atom &kb (: ann Person))
!(add-atom &kb (: bob Person))
!(add-atom &kb (: cat Person))
!(add-atom &kb (: dan Person))
!(add-atom &kb (: eve Person))
!(add-atom &kb (: fay Person))
!(add-atom &kb (: gus Person))
!(add-atom &kb (: ann-bob (≞ @(parent ann bob) ⊤)))
!(add-atom &kb (: ann-cat (≞ @(parent ann cat) ⊤)))
!(add-atom &kb (: bob-dan (≞ @(parent bob dan) ⊤)))
!(add-atom &kb (: bob-eve (≞ @(parent bob eve) ⊤)))
!(add-atom &kb (: cat-fay (≞ @(parent cat fay) ⊤)))
!(add-atom &kb (: eve-gus (≞ @(parent eve gus) ⊤)))

; The 43 pairs without a parent fact are declared false: parent is closed.
!(let $added (nu:close-binary &kb parent Person) (ClosedWorldNegatives (size-atom $added)))

(= (family-examples)
   ((Example (ann dan) ⊤) (Example (ann fay) ⊤) (Example (bob gus) ⊤)
    (Example (ann bob) ⊥) (Example (bob dan) ⊥) (Example (ann gus) ⊥)
    (Example (dan ann) ⊥) (Example (cat dan) ⊥)))

; The learned program, its sizes, and its proof: a witness for each positive
; example and a refutation at every individual for each negative one.  It is
; kept with its binders named, as ordinary code.
!(let (Learned $h $size $cost $proof)
      (nu:learn &kb (-> Person (-> Person Bool)) (family-examples) 13 400)
   (let $named (nu:named &kb (x z y w v u) $h)
     (let $stored (add-atom &self (learned grandparent $named))
       (Learned (nu:source &kb $h) (TypingSize $size) (ProofSize $cost) $proof))))

; The hypotheses the learner considered, by typing size.
!(Candidates (nu:level-counts &kb (-> Person (-> Person Bool)) 1 13))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The learned program as ordinary code             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; parent's meaning is its evidence; the domain lists its individuals.
(= (parent $a $b) (nu:holds &kb (lift app @parent (lift $a) (lift $b))))
(= (nu:individuals Person) (ann bob cat dan eve fay gus))

; All 49 pairs through the activated program, including pairs that were
; never examples, such as (ann eve).
!(let $people (nu:individuals Person)
   (match &self (learned grandparent $h)
     (let $grandparent *$h
       (GrandparentPairs
         (collapse
           (let* (($a (superpose $people))
                  ($b (superpose $people)))
             (if ($grandparent $a $b) ($a $b) (empty))))))))

; The same pairs read directly off the parent facts, for comparison.
!(Expected
   (collapse
     (match &kb (: $p1 (≞ (quote (parent $a $y)) ⊤))
       (match &kb (: $p2 (≞ (quote (parent $y $b)) ⊤)) ($a $b)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Part 2: the meta-interpretive examples' data               ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

!(bind! &mil (new-space))
!(add-atom &mil (nu:domain Person))
!(add-atom &mil (nu:bias ∧))
!(add-atom &mil (nu:bias exists))
!(add-atom &mil (: mother (-> Person (-> Person Bool))))
!(add-atom &mil (: father (-> Person (-> Person Bool))))
!(add-atom &mil (: alice Person))
!(add-atom &mil (: bob Person))
!(add-atom &mil (: carol Person))
!(add-atom &mil (: eve Person))
!(add-atom &mil (: mother-alice-bob (≞ @(mother alice bob) ⊤)))
!(add-atom &mil (: mother-alice-eve (≞ @(mother alice eve) ⊤)))
!(add-atom &mil (: father-bob-carol (≞ @(father bob carol) ⊤)))
!(add-atom &mil (: father-eve-carol (≞ @(father eve carol) ⊤)))
!(let $m (nu:close-binary &mil mother Person)
   (let $f (nu:close-binary &mil father Person)
     (ClosedWorldNegatives (size-atom $m) (size-atom $f))))

(= (mil-examples) ((Example (alice carol) ⊤) (Example (alice bob) ⊥)))

; Unrestricted: the smallest hypothesis that separates the two examples is
; "z has a father", which is not grandparent.
!(let (Learned $h $size $cost $proof)
      (nu:learn &mil (-> Person (-> Person Bool)) (mil-examples) 13 100)
   (Unrestricted (nu:source &mil $h) (TypingSize $size)))

; The chain metarule P(x, z) <- Q(x, y), R(y, z) is a lambda whose predicate
; positions are metavariables.  Typing the template fills $Q and $R; the
; binders x, y and z are the template's own.
(= (chain-metarule)
   @(lam $x (lam $z (exists Person (lam $y (∧ ($Q $x $y) ($R $y $z)))))))

(= (template-candidates $kb $template $type)
   (collapse
     (let (MkSized $s (: $h $t)) (nu:type $kb nil 20 (: $template $type)) $h)))

!(let $candidates (template-candidates &mil (chain-metarule) (-> Person (-> Person Bool)))
   (let (Explained $h $cost $proof)
        (nu:first-consistent &mil $candidates (mil-examples) 100)
     (ChainMetarule (Candidates (size-atom $candidates)) (nu:source &mil $h) (ProofSize $cost) $proof)))
