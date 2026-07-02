; Backward-chaining benchmark: Nil's typed BC, 60 entities, depth 1.
; Dialect-agnostic — runs identically on CeTTa, PeTTa, and HE.
; Produces 60 proof-carrying mortal witnesses.
;
; Timings: CeTTa ~17ms, PeTTa ~172ms, HE ~1700ms.
; Source: nilbc.metta pattern (typed rules + let* premise resolution).

;; Nat (for depth bounding)
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; Backward chainer — Nil's typed pattern (base + binary)
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

;; Unary rule support
(= (bc $kb (S $depth) (: ($rule $p1) $theorem))
   (let* (((: $rule (-> (: $p1 $t1) $theorem))
           (bc $kb $depth (: $rule (-> (: $p1 $t1) $theorem))))
          ((: $p1 $t1)
           (bc $kb $depth (: $p1 $t1))))
     (: ($rule $p1) $theorem)))

;; Knowledge base
!(bind! &kb (new-space))

;; --- Ground facts: 40 researchers, 40 engineers, 40 artists ---
;; Each is curious and persistent.

!(add-atom &kb (: r01 (researcher r01))) !(add-atom &kb (: r02 (researcher r02)))
!(add-atom &kb (: r03 (researcher r03))) !(add-atom &kb (: r04 (researcher r04)))
!(add-atom &kb (: r05 (researcher r05))) !(add-atom &kb (: r06 (researcher r06)))
!(add-atom &kb (: r07 (researcher r07))) !(add-atom &kb (: r08 (researcher r08)))
!(add-atom &kb (: r09 (researcher r09))) !(add-atom &kb (: r10 (researcher r10)))
!(add-atom &kb (: r11 (researcher r11))) !(add-atom &kb (: r12 (researcher r12)))
!(add-atom &kb (: r13 (researcher r13))) !(add-atom &kb (: r14 (researcher r14)))
!(add-atom &kb (: r15 (researcher r15))) !(add-atom &kb (: r16 (researcher r16)))
!(add-atom &kb (: r17 (researcher r17))) !(add-atom &kb (: r18 (researcher r18)))
!(add-atom &kb (: r19 (researcher r19))) !(add-atom &kb (: r20 (researcher r20)))

!(add-atom &kb (: e01 (engineer e01))) !(add-atom &kb (: e02 (engineer e02)))
!(add-atom &kb (: e03 (engineer e03))) !(add-atom &kb (: e04 (engineer e04)))
!(add-atom &kb (: e05 (engineer e05))) !(add-atom &kb (: e06 (engineer e06)))
!(add-atom &kb (: e07 (engineer e07))) !(add-atom &kb (: e08 (engineer e08)))
!(add-atom &kb (: e09 (engineer e09))) !(add-atom &kb (: e10 (engineer e10)))
!(add-atom &kb (: e11 (engineer e11))) !(add-atom &kb (: e12 (engineer e12)))
!(add-atom &kb (: e13 (engineer e13))) !(add-atom &kb (: e14 (engineer e14)))
!(add-atom &kb (: e15 (engineer e15))) !(add-atom &kb (: e16 (engineer e16)))
!(add-atom &kb (: e17 (engineer e17))) !(add-atom &kb (: e18 (engineer e18)))
!(add-atom &kb (: e19 (engineer e19))) !(add-atom &kb (: e20 (engineer e20)))

!(add-atom &kb (: a01 (artist a01))) !(add-atom &kb (: a02 (artist a02)))
!(add-atom &kb (: a03 (artist a03))) !(add-atom &kb (: a04 (artist a04)))
!(add-atom &kb (: a05 (artist a05))) !(add-atom &kb (: a06 (artist a06)))
!(add-atom &kb (: a07 (artist a07))) !(add-atom &kb (: a08 (artist a08)))
!(add-atom &kb (: a09 (artist a09))) !(add-atom &kb (: a10 (artist a10)))
!(add-atom &kb (: a11 (artist a11))) !(add-atom &kb (: a12 (artist a12)))
!(add-atom &kb (: a13 (artist a13))) !(add-atom &kb (: a14 (artist a14)))
!(add-atom &kb (: a15 (artist a15))) !(add-atom &kb (: a16 (artist a16)))
!(add-atom &kb (: a17 (artist a17))) !(add-atom &kb (: a18 (artist a18)))
!(add-atom &kb (: a19 (artist a19))) !(add-atom &kb (: a20 (artist a20)))

; Curious trait
!(add-atom &kb (: r01c (curious r01))) !(add-atom &kb (: r02c (curious r02)))
!(add-atom &kb (: r03c (curious r03))) !(add-atom &kb (: r04c (curious r04)))
!(add-atom &kb (: r05c (curious r05))) !(add-atom &kb (: r06c (curious r06)))
!(add-atom &kb (: r07c (curious r07))) !(add-atom &kb (: r08c (curious r08)))
!(add-atom &kb (: r09c (curious r09))) !(add-atom &kb (: r10c (curious r10)))
!(add-atom &kb (: r11c (curious r11))) !(add-atom &kb (: r12c (curious r12)))
!(add-atom &kb (: r13c (curious r13))) !(add-atom &kb (: r14c (curious r14)))
!(add-atom &kb (: r15c (curious r15))) !(add-atom &kb (: r16c (curious r16)))
!(add-atom &kb (: r17c (curious r17))) !(add-atom &kb (: r18c (curious r18)))
!(add-atom &kb (: r19c (curious r19))) !(add-atom &kb (: r20c (curious r20)))
!(add-atom &kb (: e01c (curious e01))) !(add-atom &kb (: e02c (curious e02)))
!(add-atom &kb (: e03c (curious e03))) !(add-atom &kb (: e04c (curious e04)))
!(add-atom &kb (: e05c (curious e05))) !(add-atom &kb (: e06c (curious e06)))
!(add-atom &kb (: e07c (curious e07))) !(add-atom &kb (: e08c (curious e08)))
!(add-atom &kb (: e09c (curious e09))) !(add-atom &kb (: e10c (curious e10)))
!(add-atom &kb (: e11c (curious e11))) !(add-atom &kb (: e12c (curious e12)))
!(add-atom &kb (: e13c (curious e13))) !(add-atom &kb (: e14c (curious e14)))
!(add-atom &kb (: e15c (curious e15))) !(add-atom &kb (: e16c (curious e16)))
!(add-atom &kb (: e17c (curious e17))) !(add-atom &kb (: e18c (curious e18)))
!(add-atom &kb (: e19c (curious e19))) !(add-atom &kb (: e20c (curious e20)))
!(add-atom &kb (: a01c (curious a01))) !(add-atom &kb (: a02c (curious a02)))
!(add-atom &kb (: a03c (curious a03))) !(add-atom &kb (: a04c (curious a04)))
!(add-atom &kb (: a05c (curious a05))) !(add-atom &kb (: a06c (curious a06)))
!(add-atom &kb (: a07c (curious a07))) !(add-atom &kb (: a08c (curious a08)))
!(add-atom &kb (: a09c (curious a09))) !(add-atom &kb (: a10c (curious a10)))
!(add-atom &kb (: a11c (curious a11))) !(add-atom &kb (: a12c (curious a12)))
!(add-atom &kb (: a13c (curious a13))) !(add-atom &kb (: a14c (curious a14)))
!(add-atom &kb (: a15c (curious a15))) !(add-atom &kb (: a16c (curious a16)))
!(add-atom &kb (: a17c (curious a17))) !(add-atom &kb (: a18c (curious a18)))
!(add-atom &kb (: a19c (curious a19))) !(add-atom &kb (: a20c (curious a20)))

;; --- Rules ---
;; researcher + curious → mortal (binary rule, one step)
!(add-atom &kb (: r-mortal (-> (: $r (researcher $x)) (: $c (curious $x)) (mortal $x))))

;; engineer + curious → mortal
!(add-atom &kb (: e-mortal (-> (: $e (engineer $x)) (: $c (curious $x)) (mortal $x))))

;; artist + curious → mortal
!(add-atom &kb (: a-mortal (-> (: $a (artist $x)) (: $c (curious $x)) (mortal $x))))

;; --- Query: find all mortal witnesses at depth 1 ---
!(bc &kb (fromNumber 1) (: $prf (mortal $x)))
