; One executable predicate, a HOL proposition, and values with evidence.
(hol:profile base)
!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &math N (u 0) (: z N) (: s (-> N N)))
!(hol:inductive &math B (u 0) (: yes B) (: no B))
!(hol:define &math flip (-> B B) (= (flip yes) no) (= (flip no) yes))
!(hol:define &math even? (-> N B)
   (= (even? z) yes)
   (= (even? (s $n)) (flip (even? $n))))

; Running a predicate answers a question about this particular input.
!(eval (even? (s (s z))) (admitted &math))
!(eval (even? (s z)) (admitted &math))

; A refinement packages the number with evidence about that same number.
!(hol:define &math EvenNumber (u 0)
   (= EvenNumber (sigma (n : N) (id B (even? n) yes))))
!(type:check &math (pair (s (s z)) (refl yes)) EvenNumber)
!(type:check &math (pair (s z) (refl yes)) EvenNumber)

; This proof function covers both Boolean constructors.
!(hol:define &math flip-twice (-> (b : B) (id B (flip (flip b)) b))
   (= (flip-twice yes) (refl yes))
   (= (flip-twice no) (refl no)))

; next-even uses the caller's evidence and returns evidence for n + 2.
!(hol:define &math next-even (-> EvenNumber EvenNumber)
   (= (next-even $v)
      (pair (s (s (fst $v)))
        (id:eliminate B (even? (fst $v))
          (lam y (lam e (id B (even? (s (s (fst $v)))) y)))
          (flip-twice (even? (fst $v))) yes (snd $v)))))
!(type:check &math next-even (-> EvenNumber EvenNumber))
!(eval (fst (next-even (pair (s (s z)) (refl yes)))) (admitted &math))
!(type:check &math (next-even (pair (s z) (refl yes))) EvenNumber)

; Claims and candidate-producing strategies are ordinary MeTTa data/programs.
; This record alone creates no theorem in &math.
(conjecture two-even-proof (id B (even? (s (s z))) yes))
!(car-atom (try (type:of &math two-even-proof)))
(= (even-proof-candidates) (superpose ((refl no) (refl yes))))
!(match &self (conjecture two-even-proof $claim)
   (type:prove &math two-even-proof $claim (even-proof-candidates)))
!(type:check &math two-even-proof (id B (even? (s (s z))) yes))

; HOL reads the same predicate through its equality proposition. The selected
; equality rules are explicit; no particular evenness claim is assumed.
!(hol:define &math Even (-> N prop)
   (= (Even $n) (eq B (even? $n) yes)))
!(add-atom &math (hol:rule hol-equality))
!(hol:theorem &math two-even (Even (s (s z))) (pf:eq-refl B yes))
!(hol:proves &math (pf:known two-even) (Even (s (s z))))
!(hol:proves &math (pf:eq-refl B yes) (Even (s z)))

; Lists use the predicate on every element, including the empty list.
!(hol:inductive &math Ns (u 0) (: nil Ns) (: cons (-> N Ns Ns)))
!(hol:define &math both (-> B B B)
   (= (both yes $b) $b) (= (both no $b) no))
!(hol:define &math all-even? (-> Ns B)
   (= (all-even? nil) yes)
   (= (all-even? (cons $n $ns)) (both (even? $n) (all-even? $ns))))
!(hol:define &math EvenNumberList (u 0)
   (= EvenNumberList (sigma (ns : Ns) (id B (all-even? ns) yes))))
!(hol:define &math length (-> Ns N)
   (= (length nil) z) (= (length (cons $n $ns)) (s (length $ns))))
!(hol:define &math even-list-length (-> EvenNumberList N)
   (= (even-list-length $v) (length (fst $v))))
!(type:check &math (pair nil (refl yes)) EvenNumberList)
!(type:check &math (pair (cons (s (s z)) (cons (s (s (s (s z)))) nil))
                            (refl yes)) EvenNumberList)
!(eval (even-list-length
    (pair (cons (s (s z)) (cons (s (s (s (s z)))) nil)) (refl yes)))
    (admitted &math))
!(type:check &math (pair (cons (s z) nil) (refl yes)) EvenNumberList)
