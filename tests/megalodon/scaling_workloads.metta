; Workloads for the native kernel's scaling check (run_scaling.sh): Megalodon
; documents, read without a signature, whose checking work should grow
; linearly with their size N.
;
;   cetta --lang petta scaling_workloads.metta KIND N OUT
;
;   decls     N independent definitions, each with a theorem about it
;   notation  N infix notations, each defined and used in a theorem
;   shared    one lemma, cited N times in the proof of one theorem
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

(= (workload $kind $n)
   (case $kind
     ((decls (str:join "" (map-atom (range-from 0 $n) $k (decl-text $k))))
      (notation (str:join "" (map-atom (range-from 0 $n) $k (notation-text $k))))
      (shared (shared-text $n)))))

; The size arrives as a symbol on the command line.
!(let ($kind $size $out) (system:args)
   (fs:write $out (workload $kind (str:string->number (str:symbol->string $size)))))
