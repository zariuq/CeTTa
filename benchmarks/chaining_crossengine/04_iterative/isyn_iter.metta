;; Row 04 — Nil Geisweiller: iterative backward chaining (isyn around curried syn).
;; Source: repos/ngeiswei-chaining-run/dtl/backward/curried.metta + utils.metta (inlined).
;; Iterative chainer: each round collapses, adds proofs back to the space, recurses
;; — so later rounds re-use inferred (macro) proofs. Mutates &kb (side-effecting).
;; Contract: (isyn &kb d2 iter1 (: $prf C)) = (: ((ModusPonens bc) ((ModusPonens ab) a)) C).
;; Runs on CeTTa (--lang he) and PeTTa.

;; ---- Nat ----
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; ---- nodup helpers ----
(: add-atom-nodup (-> $st Atom ()))
(= (add-atom-nodup $space $atom)
   (case (match $space $atom $atom)
     (($atom ())
      (Empty (add-atom $space $atom)))))
(: add-atoms-nodup (-> $st Expression ()))
(= (add-atoms-nodup $space $atoms)
   (if (== $atoms ())
       ()
       (let* (($head (car-atom $atoms))
              ($tail (cdr-atom $atoms))
              ($dummy (add-atom-nodup $space $head)))
         (add-atoms-nodup $space $tail))))

;; ---- Curried backward chainer ----
(: syn (-> $a Nat $b $b))
(= (syn $kb $_ (: $prf $ccln)) (match $kb (: $prf $ccln) (: $prf $ccln)))
(= (syn $kb (S $k) (: ($prfabs $prfarg) $ccln))
   (let* (((: $prfabs (-> $prms $ccln)) (syn $kb $k (: $prfabs (-> $prms $ccln))))
          ((: $prfarg $prms) (syn $kb $k (: $prfarg $prms))))
     (: ($prfabs $prfarg) $ccln)))

;; ---- Iterative wrapper ----
(: isyn (-> $a Nat Nat $b $b))
(= (isyn $kb $depth Z $query) $query)
(= (isyn $kb $depth (S $k) $query)
   (let* (($cres (collapse (syn $kb $depth $query)))
          ($dummy (add-atoms-nodup $kb $cres)))
     (isyn $kb $depth $k (superpose $cres))))

;; ---- Knowledge + rule base ----
!(bind! &kb (new-space))
!(add-atom &kb (: ab (→ A B)))
!(add-atom &kb (: bc (→ B C)))
!(add-atom &kb (: a A))
!(add-atom &kb (: ModusPonens (-> (→ $p $q) (-> $p $q))))
!(add-atom &kb (: Deduction (-> (→ $q $r) (-> (→ $p $q) (→ $p $r)))))
!(add-atom &kb (: . (-> (-> $q $r) (-> (-> $p $q) (-> $p $r)))))

;; ---- Queries: prove B first (adds its proof to &kb), then C re-uses it ----
;; This is the iterative-chaining point: depth-2/iter-1 proves B, and the C
;; query then closes at depth 2 only because B's proof was added to the space.
!(isyn &kb (fromNumber 2) (fromNumber 1) (: $prf B))
!(isyn &kb (fromNumber 2) (fromNumber 1) (: $prf C))
