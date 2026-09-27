; Learning a recursive relation: ancestor.
;
; A recursive hypothesis is (fix (lam self body)): the recursive predicate is
; an ordinary binder, so the language manages its scope like any other.
; Unfolding opens that binder at a fresh copy of the hypothesis.
;
; The tail-recursion metarule of meta-interpretive learning,
;   P(x, z) <- Q(x, z)  or  exists y. R(x, y) and P(y, z),
; is a lambda template whose predicate positions are metavariables.  Typing
; the template fills them, from the knowledge base or with the recursive
; predicate itself.  Unrestricted enumeration would have to pass every
; hypothesis smaller than the target, whose typing size is 20.

!(import! &self nupln_lambda_core.metta)

!(bind! &kb (new-space))
!(add-atom &kb (nu:domain Person))
!(add-atom &kb (nu:bias ∧))
!(add-atom &kb (nu:bias ∨))
!(add-atom &kb (nu:bias exists))
!(add-atom &kb (nu:bias fix))
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
!(let $added (nu:close-binary &kb parent Person) (ClosedWorldNegatives (size-atom $added)))

; Examples at depths one to three, and pairs that are not ancestors.
(= (ancestor-examples)
   ((Example (ann bob) ⊤) (Example (ann dan) ⊤) (Example (ann gus) ⊤)
    (Example (bob gus) ⊤)
    (Example (bob ann) ⊥) (Example (dan ann) ⊥) (Example (cat dan) ⊥)
    (Example (fay gus) ⊥) (Example (bob cat) ⊥)))

(= (tail-recursion)
   @(fix (lam $self (lam $x (lam $z
      (∨ ($Q $x $z)
         (exists Person (lam $y (∧ ($R $x $y) ($self $y $z))))))))))

(= (template-candidates $kb $template $type)
   (collapse
     (let (MkSized $s (: $h $t)) (nu:type $kb nil 40 (: $template $type)) $h)))

; The candidates: Q and R are each parent or the recursive predicate.
!(let $candidates (template-candidates &kb (tail-recursion) (-> Person (-> Person Bool)))
   (Candidates (map-atom $candidates $h (nu:source &kb $h))))

; The first candidate, in order, that explains the examples, and its proof
; size.  Candidates whose base case calls the recursive predicate itself
; would only recurse until the proof budget runs out, so the search stops at
; the first success.
!(let $candidates (template-candidates &kb (tail-recursion) (-> Person (-> Person Bool)))
   (let (Explained $h $cost $proof)
        (nu:first-in-order &kb $candidates (ancestor-examples) 200)
     (let $named (nu:named &kb (x z y w v u) $h)
       (let $stored (add-atom &self (learned ancestor $named))
         (Learned (nu:source &kb $h) (ProofSize $cost))))))

; The proof for a depth-three example: bob, then eve, then the parent fact
; eve-gus, with one unfolding per step down.
!(let $candidates (template-candidates &kb (tail-recursion) (-> Person (-> Person Bool)))
   (let $h (car-atom $candidates)
     (let $s (nu:at $h (ann gus))
       (let (MkSized $n (: $prf $j)) (nu:first (nu:eval &kb 200 (: $prf (≞ $s ⊤))))
         (Proof (ann gus) $prf (ProofSize $n))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The learned program as ordinary code ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(= (parent $a $b) (nu:holds &kb @(parent $a $b)))
(= (nu:individuals Person) (ann bob cat dan eve fay gus))

; Every ancestor pair, computed by running the activated recursive program.
!(let $people (nu:individuals Person)
   (match &self (learned ancestor $h)
     (let $ancestor *$h
       (AncestorPairs
         (collapse
           (let* (($a (superpose $people))
                  ($b (superpose $people)))
             (if ($ancestor $a $b) ($a $b) (empty))))))))
