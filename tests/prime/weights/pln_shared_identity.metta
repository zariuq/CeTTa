!(import! &self ../../../lib/native_weight_algebras.metta)
!(import! &self ../../../lib/lib_wmpln_evidence.metta)
!(pragma! prefer-rationals True)

; This client declares natural evidence counts. The algebra descriptor alone
; does not validate that domain. An unrecognized packet leaves admission open.
(= (pln:count-shape? $packet)
   (if (== (get-metatype $packet) Expression)
     (if (== (weights:length $packet) 3)
       (== (weights:at $packet 0) evidence)
       False)
     False))
(= (pln:count-profile? $packet)
   (if (pln:count-shape? $packet)
     (let* (($pos (evidence-pos $packet)) ($neg (evidence-neg $packet)))
       (if (and (weight:exact-rational? $pos) (weight:exact-rational? $neg))
         (and (weights:natural? $pos) (weights:natural? $neg))
         False))
     False))
(= (pln:positive-count? $packet)
   (if (pln:count-profile? $packet)
     (> (evidence-pos $packet) 0)
     CountOutsideProfile))

; A nonzero packet is a different admission policy: opposite polarities are
; each nonzero, but their componentwise tensor is zero.
(= (pln:nonzero-count? $packet)
   (if (pln:count-profile? $packet)
     (not (== $packet (evidence 0 0)))
     CountOutsideProfile))

(=w (pln:coin) 2 (evidence 3 1))
(=w (pln:coin) 2 (evidence 3 1))
(= (pln:coin) 5 (where (evidence 7 1)))
(= (weigh-by-self $x) $x
   (where (evidence $x 1) (admit pln:positive-count?)))
(=w (pln:literal-id $x) $x (evidence 2 1) (admit pln:positive-count?))
(= (pln:paired $x) (Pair $x $x))

; Each use of the identity's result retains the one selected argument world.
!(weight:read (weights:pln) (weigh-by-self (weigh-by-self 7)))
!(weight:read (weights:pln) (pln:literal-id (pln:literal-id 7)))
!(weight:read (weights:pln) (pln:paired (weigh-by-self (pln:coin))))
!(select 7 (search-policy (control FIFO (budget 10000) (workers 2)))
   (weight:read (weights:pln) (pln:paired (weigh-by-self (pln:coin)))))
