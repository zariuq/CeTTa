; Backward-chaining benchmark for CeTTa's HE lane.
; Final query should produce 20 mortal witnesses under --count-only.

(Evaluation (philosopher p1))
(Evaluation (philosopher p2))
(Evaluation (philosopher p3))
(Evaluation (philosopher p4))
(Evaluation (philosopher p5))
(Evaluation (philosopher p6))
(Evaluation (philosopher p7))
(Evaluation (philosopher p8))

(Evaluation (stoic s1))
(Evaluation (stoic s2))
(Evaluation (stoic s3))
(Evaluation (stoic s4))
(Evaluation (stoic s5))
(Evaluation (stoic s6))

(Evaluation (mathematician m1))
(Evaluation (mathematician m2))
(Evaluation (mathematician m3))
(Evaluation (mathematician m4))
(Evaluation (mathematician m5))
(Evaluation (mathematician m6))

(Evaluation (likes-to-wrestle p1))
(Evaluation (likes-to-wrestle p2))
(Evaluation (likes-to-wrestle p3))
(Evaluation (likes-to-wrestle p4))
(Evaluation (likes-to-wrestle p5))
(Evaluation (likes-to-wrestle p6))
(Evaluation (likes-to-wrestle p7))
(Evaluation (likes-to-wrestle p8))
(Evaluation (likes-to-wrestle s1))
(Evaluation (likes-to-wrestle s2))
(Evaluation (likes-to-wrestle s3))
(Evaluation (likes-to-wrestle s4))
(Evaluation (likes-to-wrestle s5))
(Evaluation (likes-to-wrestle s6))
(Evaluation (likes-to-wrestle m1))
(Evaluation (likes-to-wrestle m2))
(Evaluation (likes-to-wrestle m3))
(Evaluation (likes-to-wrestle m4))
(Evaluation (likes-to-wrestle m5))
(Evaluation (likes-to-wrestle m6))

(Implication
   (Evaluation (stoic $x))
   (Evaluation (philosopher $x)))

(Implication
   (Evaluation (mathematician $x))
   (Evaluation (philosopher $x)))

(Implication
   (And (Evaluation (philosopher $x))
        (Evaluation (likes-to-wrestle $x)))
   (Evaluation (human $x)))

(Implication
   (Evaluation (human $x))
   (Evaluation (mortal $x)))

(= (deduce (Evaluation ($P $x)))
   (match &self (Evaluation ($P $x)) T))

(= (deduce (Evaluation ($P $x)))
   (match &self
     (Implication $a (Evaluation ($P $x)))
     (deduce $a)))

(= (deduce (And $a $b))
   (And (deduce $a) (deduce $b)))

(= (And T T) T)
(= (ift T $then) $then)

!(ift (deduce (Evaluation (mortal $x))) $x)
