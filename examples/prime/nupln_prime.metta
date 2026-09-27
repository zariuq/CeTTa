;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; nuPLN with lambda hypotheses in HO MeTTa Prime                            ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; A companion to Nil Geisweiller's experimental/nupln/nupln.metta.
;; Run from the CeTTa Prime draft:
;;
;;   ./cetta --lang prime examples/prime/nupln_prime.metta
;;
;; We keep Nil's named evidence, backward chaining, proof constructors and
;; eleven examples.  The change is how we represent a predicate.  Instead of
;; the pointwise combinator (∧ F G), we build the ordinary function
;;
;;                 @(lam $x (∧ (F $x) (G $x)))
;;
;; Three roles must stay distinct:
;;   $h       a search metavariable: which program shall we learn?
;;   lam $x   an object-language binder inside that program
;;   @...     the program as code; * activates a code value
;;
;; Learning builds and inspects code.  Afterwards the same lambda runs as a
;; function.  No combinator translation is needed in the user program.
;;
;; Scope of this example: Boolean evidence (⊤ / ⊥), finite search, explicit
;; evidence trees.  These trees are checked by the MeTTa rules in the imported
;; learner; they are not yet certificates checked by Prime's native proof
;; kernel.  Probabilistic strengths, confidence and stochastic worlds are
;; separate extensions.  This is not a port of Nil's random sampler.
;;
;; The imported draft implementation handles its generated binder patterns.
;; It is not a general hygienic macro facility: the separate metaprogramming
;; probes include capture failures.  The learner is shared with those examples
;; rather than copied here.

!(import! &self nupln_lambda/nupln_lambda_core.metta)

;;;;;;;;;;;;;;;;;;;;
;; Knowledge base ;;
;;;;;;;;;;;;;;;;;;;;

!(bind! &kb (new-space))
!(add-atom &kb (nu:domain UInt))
!(add-atom &kb (nu:bias ∧))
!(add-atom &kb (: 0 UInt))
!(add-atom &kb (: 1 UInt))
!(add-atom &kb (: 2 UInt))
!(add-atom &kb (: F (-> UInt Bool)))
!(add-atom &kb (: G (-> UInt Bool)))

;; A fact names its evidence.  f0t witnesses that F holds at 0; f1f witnesses
;; that F is false at 1.  Absence of evidence is neither truth nor falsity.
!(add-atom &kb (: f0t (≞ @(F 0) ⊤)))
!(add-atom &kb (: f1f (≞ @(F 1) ⊥)))
!(add-atom &kb (: f2t (≞ @(F 2) ⊤)))
!(add-atom &kb (: g0t (≞ @(G 0) ⊤)))
!(add-atom &kb (: g1t (≞ @(G 1) ⊤)))
!(add-atom &kb (: g2f (≞ @(G 2) ⊥)))

;; An existing hypothesis, to explain the judgment forms before learning one.
(= (conjunction) @(lam $x (∧ (F $x) (G $x))))

;;;;;;;;;;;;
;; Typing ;;
;;;;;;;;;;;;

;; nu:type takes a knowledge base, a context, a derivation-size bound and a
;; typing goal.  nil is the empty context.  An answer is (MkSized n judgment).
;; $type is unknown here: these queries infer it.
!(Test 1 (nu:type &kb nil 1 (: @F $type)))
!(Test 2 (nu:type &kb nil 1 (: @G $type)))

;; Test 3: the predicate has type UInt -> Bool.  nu:source gives stable,
;; readable binder names; it does not run the predicate.
!(let (MkSized $n (: $h $type))
      (nu:type &kb nil 8 (: (conjunction) $type))
   (Test 3 (nu:source &kb $h) $type (TypingSize $n)))

;; Test 4: the individual itself is a term, not a singleton combinator.
!(Test 4 (nu:type &kb nil 1 (: @0 $type)))

;; Test 5: ordinary function application has type Bool.
!(let (MkSized $n (: (quote ($h $c)) $type))
      (nu:type-app &kb 10 (conjunction) @0)
   (Test 5 (Application (nu:source &kb @$h) $c) $type (TypingSize $n)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Evaluation from evidence ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; nu:at instantiates the lambda's binders, keeping the result as code.
;; nu:eval derives a judgment from evidence; it does not activate that code.
;; cet joins two positive witnesses.  cefl and cefr retain the left or right
;; counterexample.  These are Nil's proof constructors.
!(let $statement (nu:at (conjunction) (0))
   (let (MkSized $n (: $proof $judgment))
        (nu:eval &kb 3 (: $proof (≞ $statement $tv)))
     (Test 6 $proof $judgment (ProofSize $n)
       (SameProof (== $proof (cet f0t g0t))))))

!(let $statement (nu:at (conjunction) (1))
   (let (MkSized $n (: $proof $judgment))
        (nu:eval &kb 2 (: $proof (≞ $statement $tv)))
     (Test 7 $proof $judgment (ProofSize $n)
       (SameProof (== $proof (cefl f1f))))))

;; A comma is the product of two proof obligations.  It is the constructor
;; from Nil's learner, not punctuation for a simultaneous substitution.
!(let (MkSized $n (: $proof $judgment))
      (nu:eval &kb 5 (: $proof
        (, (≞ @(F 0) ⊤) (, (≞ @(F 1) ⊥) (≞ @(F 2) ⊤)))))
   (Test 8 $proof (ProofSize $n)
     (SameProof (== $proof (, f0t (, f1f f2t))))))

;; The examples say what a candidate must explain.  nu:obligations makes the
;; conjunction of those requirements; the supplied labels are not inferred.
(= (training)
   ((Example (0) ⊤) (Example (1) ⊥) (Example (2) ⊥)))

!(let $goal (nu:obligations (conjunction) ((Example (0) ⊤) (Example (1) ⊥)))
   (let (MkSized $n (: $proof $judgment)) (nu:eval &kb 6 (: $proof $goal))
     (Test 9 $proof (ProofSize $n)
       (SameProof (== $proof (, (cet f0t g0t) (cefl f1f)))))))

!(let $goal (nu:obligations (conjunction) (training))
   (let (MkSized $n (: $proof $judgment)) (nu:eval &kb 9 (: $proof $goal))
     (Test 10 $proof (ProofSize $n)
       (SameProof (== $proof
         (, (cet f0t g0t) (, (cefl f1f) (cefr g2f))))))))

;;;;;;;;;;;;;;
;; Learning ;;
;;;;;;;;;;;;;;

;; Test 11: now leave the whole predicate unknown.  The declared hypothesis
;; language allows UInt binders, F, G and conjunction.  Search increases the
;; typing-size bound; it returns the first fitting program in that order.
;; This does not establish that the learned hypothesis is uniquely true.
;;
;; 12 bounds hypothesis size; 27 bounds each example's evidence search.
;; ProofSize is a derivation measure, not elapsed time or a probability.
;; This example's evidence tree has size 9.  Nil's original has size 27,
;; including three typing derivations omitted by this evaluator.  These are
;; different accounting conventions, not a claim of a threefold speedup.
!(let (Learned $h $size $cost $proof)
      (nu:learn &kb (-> UInt Bool) (training) 12 27)
   (let $named (nu:named &kb (x y z) $h)
     (let $stored (add-atom &self (learned-predicate $named))
       (Test 11 (nu:source &kb $h) (TypingSize $size) (ProofSize $cost)
         $proof (SameProof (== $proof
           (, (cet f0t g0t) (, (cefl f1f) (cefr g2f)))))))))

;;;;;;;;;;;;;;;;;;;;;;
;; Running the code ;;
;;;;;;;;;;;;;;;;;;;;;;

;; The evidence table above was sufficient for learning.  To run the learned
;; function, give its primitive predicates executable definitions too.
;; Here we deliberately define them beyond the three observed individuals.
(= (F $x) (== (% $x 2) 0))
(= (G $x) (< $x 2))

;; No * means code remains code.  Activation produces a function, which can
;; then receive an ordinary argument.  x is the lambda's bound object variable;
;; $f below is a metavariable holding that activated function.
!(match &self (learned-predicate $h)
   (Stored (nu:source &kb $h)))

!(match &self (learned-predicate $h)
   (let $f *$h
     (Run (collapse
       (let $x (superpose (0 1 2 3)) (At $x ($f $x)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; What failure does not assert ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; F(3) runs to False, but the evidence space contains no observation of it.
;; Both truth-value searches below are empty.  Running and proving relative
;; to an evidence space answer different questions.
!(Unknown-positive (collapse (nu:eval &kb 4 (: $proof (≞ @(F 3) ⊤)))))
!(Unknown-negative (collapse (nu:eval &kb 4 (: $proof (≞ @(F 3) ⊥)))))

;; A false claim about an observed case also has no proof in this bounded
;; search.  Unlike F(3), it has explicit counterevidence, shown alongside it.
!(Wrong-label (collapse (nu:eval &kb 4 (: $proof (≞ @(F 1) ⊤)))))
!(Counterevidence (nu:eval &kb 1 (: $proof (≞ @(F 1) ⊥))))

;; A limit of 4 cannot reach the conjunction, whose typing size is 8.
;; NotLearned means this search did not find a hypothesis within its bounds.
!(Small-budget (nu:learn &kb (-> UInt Bool) (training) 4 27))

;;;;;;;;;;;;;;;;;;;
;; Going further ;;
;;;;;;;;;;;;;;;;;;;
;;
;; nupln_lambda/grandparent.metta learns
;;   lam x (lam z (exists Person (lam y (∧ (parent x y) (parent y z))))).
;; Its hidden witness y is an ordinary scoped variable.  A held-out pair tests
;; generalization.  A second experiment restricts search to a chain template.
;;
;; nupln_lambda/ancestor.metta instantiates a recursive template.  That is a
;; useful search bias, not unrestricted discovery of recursion.
;;
;; nupln_lambda/raven_induction.metta starts from evidence counts and prints
;; exact strengths and confidences.  It is separate from this Boolean learner.
;;
;; metaprogramming/show_01_lambda_synthesizer.metta gathers observations by
;; running a library, learns from them without further library calls, then
;; activates the learned lambda on previously unlabelled inputs.
