; Define, inspect, compute, conjecture, search, check, publish and reuse.
(hol:profile base)
!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &math nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:inductive &math list (u 0) (: nil list) (: cons (-> nat list list)))
!(hol:define &math add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))
!(hol:define &math double (-> nat nat) (= (double $n) (add $n $n)))
!(hol:define &math mapf (-> (-> nat nat) list list)
   (= (mapf $f nil) nil)
   (= (mapf $f (cons $x $xs)) (cons ($f $x) (mapf $f $xs))))
!(hol:define &math comp (-> (-> nat nat) (-> nat nat) nat nat)
   (= (comp $f $g $x) ($f ($g $x))))
!(hol:define &math length (-> list nat)
   (= (length nil) zero)
   (= (length (cons $x $xs)) (succ (length $xs))))
; The declared list observation counts elements: nil ends, so this is two.
!(type:normalize &math (length (cons zero (cons zero nil))))

!(type:of &math mapf)
!(collapse (match &math (hol:known mapf $type $origin $body $dependencies $revision $signature)
   (Definition $origin $body)))
!(type:normalize &math (mapf succ (mapf double (cons (succ zero) nil))))
; The ordinary evaluator can select the admitted rules explicitly. Its
; readable result is checked and returned as data.
!(eval (mapf succ (mapf double (cons (succ zero) nil))) (admitted &math))

; Statements and strategies are ordinary programs returning syntax.
(= (fusion-statement)
   (-> (f : (-> nat nat)) (g : (-> nat nat)) (xs : list)
       (id list (mapf f (mapf g xs)) (mapf (comp f g) xs))))
(= (fusion-proof)
   (lam f (lam g (lam xs
     (list-rec (lam l (id list (mapf f (mapf g l)) (mapf (comp f g) l)))
       (refl nil)
       (lam x (lam l (lam ih
         (id:eliminate list (mapf f (mapf g l))
           (lam y (lam e (id list (cons (f (g x)) (mapf f (mapf g l)))
                                  (cons (f (g x)) y))))
           (refl (cons (f (g x)) (mapf f (mapf g l))))
           (mapf (comp f g) l) ih))))
       xs)))))
(= (fusion-strategy)
   (superpose ((lam f (lam g (lam xs (refl (mapf f (mapf g xs))))))
               (fusion-proof))))

; Inspection returns the strategy's code without executing it.
!(collapse (match &self (= (fusion-strategy) $body) (Strategy $body)))
!(let $claim (fusion-statement)
   (let $proof (fusion-proof) (type:check &math $proof $claim)))
!(let $claim (fusion-statement)
   (type:check &math (lam f (lam g (lam xs (refl (mapf f (mapf g xs)))))) $claim))
!(let $claim (fusion-statement)
   (type:prove &math fusion $claim (fusion-strategy)))
!(let $claim (fusion-statement) (type:check &math fusion $claim))
; Publication belongs to &math; merely using the same name elsewhere fails.
!(car-atom (try (type:of fusion)))

; A checked universal theorem licenses the alternative pure computation.
; The pair also retains evidence about the exact original expression.
!(hol:define &math fuseMap
   (-> (f : (-> nat nat)) (g : (-> nat nat))
       (law : (-> (xs : list) (id list (mapf f (mapf g xs)) (mapf (comp f g) xs))))
       (xs : list) (sigma (ys : list) (id list (mapf f (mapf g xs)) ys)))
   (= (fuseMap $f $g $law $xs)
      (pair (mapf (comp $f $g) $xs) ($law $xs))))
!(type:normalize &math
   (fst (fuseMap succ double (fusion succ double) (cons (succ zero) nil))))
!(type:check &math
   (snd (fuseMap succ double (fusion succ double) (cons (succ zero) nil)))
   (id list (mapf succ (mapf double (cons (succ zero) nil)))
            (mapf (comp succ double) (cons (succ zero) nil))))

; A programmer can choose an optimization by inspecting syntax. Its output
; must carry equality evidence about the caller's exact source expression.
!(hol:define &math useEqual
   (-> (A : (u 0)) (source : A)
       (candidate : (sigma (value : A) (id A source value))) A)
   (= (useEqual $A $source $candidate) (fst $candidate)))
(: propose-fusion (-> Proc Proc))
(= (propose-fusion $source)
   (case $source
     (((mapf $f (mapf $g $xs)) (fuseMap $f $g (fusion $f $g) $xs)))))
(workspace-expression nested-map (mapf succ (mapf double (cons (succ zero) nil))))
!(match &self (workspace-expression nested-map $source)
   (let $candidate (propose-fusion $source)
     (type:normalize &math (useEqual list $source $candidate))))
!(match &self (workspace-expression nested-map $source)
   (let $candidate (propose-fusion $source)
     (eval (useEqual list $source $candidate) (admitted &math))))
; An optimizer may propose this pair, but its proof concerns nil rather
; than the actual nonempty source. It earns no result from the checker.
!(car-atom (try (type:normalize &math
   (useEqual list (mapf succ (cons zero nil)) (pair nil (refl nil))))))

; The same certificate cannot authorize the reversed composition.
!(type:check &math (fusion succ double)
   (-> (xs : list) (id list (mapf succ (mapf double xs)) (mapf (comp double succ) xs))))
!(car-atom (try (type:normalize &math
   (fst (fuseMap succ double unknown-law (cons (succ zero) nil))))))
!(car-atom (try (type:normalize &math
   (mapf succ (cons zero nil)) 1)))

; An existing independent proof route joins the same workspace. The hosted
; HOL Light state retains requests; transport replays them in the target.
!(import! &self ../../lib/prime/hol_light/data.metta)
!(import! &self ../../lib/prime/hol_light/syntax.metta)
!(import! &self ../../lib/prime/hol_light/kernel.metta)
!(import! &self ../../lib/prime/hol_light/definitions.metta)
!(import! &self ../../lib/prime/hol_light/derived.metta)
!(import! &self ../../lib/prime/hol_light/profile.metta)
!(import! &self ../../lib/prime/hol_light/transport.metta)
!(add-atom &math (hol:rule hol-equality))
!(let $x (Var "x" (Tyapp "ind" HOL:Nil))
   (let $steps (HOL:Cons (HOL:Step "identity-beta" (BETA (Comb (Abs $x $x) $x))) HOL:Nil)
     (case (hol-light:standard-run $steps)
       (((Some $state)
         (let $name (hol-light:transport-replay &math (HOL:Interpretation nat)
                    $state "identity-beta" HOL:Nil)
           (Transported $name (hol:known-proposition &math $name)
             (hol:recheck &math $name))))))))
!(TransportCommitments (hol-light:transport-commitments &math))
