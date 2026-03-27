; PeTTa-native backward chaining: exercises the PeTTa→HE translator.
; Uses PeTTa-specific prog1 (translator converts to nested let with fresh vars).
; 30 entities, 3 binary rules, depth 1. Produces 30 proof-carrying witnesses.
;
; Translate PeTTa→HE: swipl -l test_on_real_files.pl -g "..." (see translators/)
; Timings: CeTTa ~15ms (translated), PeTTa ~151ms (native), HE ~1010ms (translated).

;; Nat (for depth bounding)
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; Backward chainer — Nil's typed pattern
(= (bc $kb $_ (: $proof $theorem))
   (match $kb (: $proof $theorem) (: $proof $theorem)))

(= (bc $kb (S $depth) (: ($rule $p1 $p2) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) (: $p2 $t2) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) (: $p2 $t2) $theorem))))
          ((: $p1 $t1)
           (bc $kb $depth (: $p1 $t1)))
          ((: $p2 $t2)
           (bc $kb $depth (: $p2 $t2))))
     (: ($rule $p1 $p2) $theorem)))

(= (bc $kb (S $depth) (: ($rule $p1) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) $theorem))))
          ((: $p1 $t1)
           (bc $kb $depth (: $p1 $t1))))
     (: ($rule $p1) $theorem)))

;; Knowledge base
!(bind! &kb (new-space))

;; 30 entities (10 per category)
!(add-atom &kb (: r01 (researcher r01))) !(add-atom &kb (: r02 (researcher r02)))
!(add-atom &kb (: r03 (researcher r03))) !(add-atom &kb (: r04 (researcher r04)))
!(add-atom &kb (: r05 (researcher r05))) !(add-atom &kb (: r06 (researcher r06)))
!(add-atom &kb (: r07 (researcher r07))) !(add-atom &kb (: r08 (researcher r08)))
!(add-atom &kb (: r09 (researcher r09))) !(add-atom &kb (: r10 (researcher r10)))

!(add-atom &kb (: e01 (engineer e01))) !(add-atom &kb (: e02 (engineer e02)))
!(add-atom &kb (: e03 (engineer e03))) !(add-atom &kb (: e04 (engineer e04)))
!(add-atom &kb (: e05 (engineer e05))) !(add-atom &kb (: e06 (engineer e06)))
!(add-atom &kb (: e07 (engineer e07))) !(add-atom &kb (: e08 (engineer e08)))
!(add-atom &kb (: e09 (engineer e09))) !(add-atom &kb (: e10 (engineer e10)))

!(add-atom &kb (: a01 (artist a01))) !(add-atom &kb (: a02 (artist a02)))
!(add-atom &kb (: a03 (artist a03))) !(add-atom &kb (: a04 (artist a04)))
!(add-atom &kb (: a05 (artist a05))) !(add-atom &kb (: a06 (artist a06)))
!(add-atom &kb (: a07 (artist a07))) !(add-atom &kb (: a08 (artist a08)))
!(add-atom &kb (: a09 (artist a09))) !(add-atom &kb (: a10 (artist a10)))

;; Curious trait
!(add-atom &kb (: r01c (curious r01))) !(add-atom &kb (: r02c (curious r02)))
!(add-atom &kb (: r03c (curious r03))) !(add-atom &kb (: r04c (curious r04)))
!(add-atom &kb (: r05c (curious r05))) !(add-atom &kb (: r06c (curious r06)))
!(add-atom &kb (: r07c (curious r07))) !(add-atom &kb (: r08c (curious r08)))
!(add-atom &kb (: r09c (curious r09))) !(add-atom &kb (: r10c (curious r10)))
!(add-atom &kb (: e01c (curious e01))) !(add-atom &kb (: e02c (curious e02)))
!(add-atom &kb (: e03c (curious e03))) !(add-atom &kb (: e04c (curious e04)))
!(add-atom &kb (: e05c (curious e05))) !(add-atom &kb (: e06c (curious e06)))
!(add-atom &kb (: e07c (curious e07))) !(add-atom &kb (: e08c (curious e08)))
!(add-atom &kb (: e09c (curious e09))) !(add-atom &kb (: e10c (curious e10)))
!(add-atom &kb (: a01c (curious a01))) !(add-atom &kb (: a02c (curious a02)))
!(add-atom &kb (: a03c (curious a03))) !(add-atom &kb (: a04c (curious a04)))
!(add-atom &kb (: a05c (curious a05))) !(add-atom &kb (: a06c (curious a06)))
!(add-atom &kb (: a07c (curious a07))) !(add-atom &kb (: a08c (curious a08)))
!(add-atom &kb (: a09c (curious a09))) !(add-atom &kb (: a10c (curious a10)))

;; Rules
!(add-atom &kb (: r-mortal (-> (: $r (researcher $x)) (: $c (curious $x)) (mortal $x))))
!(add-atom &kb (: e-mortal (-> (: $e (engineer $x)) (: $c (curious $x)) (mortal $x))))
!(add-atom &kb (: a-mortal (-> (: $a (artist $x)) (: $c (curious $x)) (mortal $x))))

;; Query wrapper — uses prog1 (PeTTa-specific) inside an equation
(= (run-query)
   (prog1
     (bc &kb (fromNumber 1) (: $prf (mortal $x)))
     (println! "query-complete")))

!(run-query)
