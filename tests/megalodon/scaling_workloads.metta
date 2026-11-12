; Workloads for the native kernel's scaling check (run_scaling.sh): Megalodon
; documents, read without a signature, whose checking work should grow
; linearly with their size N.
;
;   cetta --lang petta scaling_workloads.metta KIND N OUT
;
;   decls     N independent definitions, each with a theorem about it
;   notation  N infix notations, each defined and used in a theorem
;   shared    one lemma, cited N times in the proof of one theorem
;   nearly    N definitions, each a set former over the one before, and one
;             rewrite that passes a term nearly equal to its own side: the
;             two are N definitions deep and differ at the bottom
;
; N = 0 gives the empty document, whose cost the check subtracts.

!(import! &self fs)
!(import! &self str)
!(import! &self system)

(= (range-from $i $n) (if (>= $i $n) () (cons-atom $i (range-from (+ $i 1) $n))))

(= (decl-text $k)
   (format-args "Definition scd{} : prop -> prop := fun p => p.
Theorem sct{} : forall p:prop, p -> scd{} p.
let p. assume H: p. exact H.
Qed.
" ($k $k $k)))

(= (notation-text $k)
   (format-args "Definition scf{} : prop -> prop -> prop := fun p q => p.
Infix scop{} 500 left := scf{}.
Theorem scn{} : forall p q:prop, p -> p scop{} q.
let p q. assume H: p. exact H.
Qed.
" ($k $k $k $k $k)))

(= (shared-claim $k)
   (format-args "claim C{}: p -> p.
{} exact scl p. {}
" ($k "{" "}")))
(= (shared-text $n)
   (if (== $n 0) ""
       (format-args "Theorem scl : forall p:prop, p -> p.
let p. assume H: p. exact H.
Qed.
Theorem scs : forall p:prop, p -> p.
let p.
{}exact C{}.
Qed.
" ((str:join "" (map-atom (range-from 0 $n) $k (shared-claim $k))) (- $n 1)))))

; scwK x is the set former over x with scw(K-1) as its function, so one
; unfolding gives a term whose first argument is x.  sctop composes scwN down
; to scw1; scnear is sctop with one more Power at the bottom.  The rewrite's
; side is "scnear a" and the claim holds "sctop b" before it, so the rewrite
; first asks whether scnear and sctop are convertible.  They are not, and at
; each of the N levels that is seen only after the level below was compared.
(= (nearly-def $k)
   (if (== $k 1)
       "Definition scw1 : set -> set := fun x => Repl x Power.
"
       (format-args "Definition scw{} : set -> set := fun x => Repl x scw{}.
" ($k (- $k 1)))))
(= (nearly-tower $k $inner)
   (if (== $k 0) $inner (format-args "scw{} ({})" ($k (nearly-tower (- $k 1) $inner)))))
(= (nearly-text $n)
   (if (== $n 0) ""
       (format-args "Section Eq.
Variable A:SType.
Definition eq : A->A->prop := fun x y:A => forall Q:A->A->prop, Q x y -> Q y x.
End Eq.
Infix = 502 := eq.
Parameter Power : set->set.
Parameter Repl : set -> (set -> set) -> set.
{}Definition sctop : set -> set := fun x => {}.
Definition scnear : set -> set := fun x => {}.
Theorem scnearly : forall P:set -> set -> prop, forall a b:set,
  scnear a = a -> P (sctop b) a -> P (sctop b) (scnear a).
let P a b. assume H: scnear a = a. assume H2: P (sctop b) a.
rewrite H.
exact H2.
Qed.
" ((str:join "" (map-atom (range-from 1 (+ $n 1)) $k (nearly-def $k)))
   (nearly-tower $n "x")
   (nearly-tower $n "Power x")))))

(= (workload $kind $n)
   (case $kind
     ((decls (str:join "" (map-atom (range-from 0 $n) $k (decl-text $k))))
      (notation (str:join "" (map-atom (range-from 0 $n) $k (notation-text $k))))
      (shared (shared-text $n))
      (nearly (nearly-text $n)))))

; The size arrives as a symbol on the command line.
!(let ($kind $size $out) (system:args)
   (fs:write $out (workload $kind (str:string->number (str:symbol->string $size)))))
