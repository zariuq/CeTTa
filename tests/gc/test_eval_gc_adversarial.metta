; Adversarial witnesses for tail-safe eval GC.
;
; These cases are intentionally small enough for the normal test lane, while
; scripts/gc_adversarial_audit.sh reruns them with a tiny GC budget to force
; collections through the same semantic paths.

(= (gc-payload $n)
   (payload $n
     a00 a01 a02 a03 a04 a05 a06 a07
     a08 a09 a10 a11 a12 a13 a14 a15
     a16 a17 a18 a19 a20 a21 a22 a23
     a24 a25 a26 a27 a28 a29 a30 a31
     a32 a33 a34 a35 a36 a37 a38 a39
     a40 a41 a42 a43 a44 a45 a46 a47
     a48 a49 a50 a51 a52 a53 a54 a55
     a56 a57 a58 a59 a60 a61 a62 a63))

(= (gc-countdown $n $acc)
   (if (== $n 0)
       $acc
       (let $dead (gc-payload $n)
         (gc-countdown (- $n 1) (+ $acc 1)))))

!(assertEqual (gc-countdown 100 0) 100)

(= (gc-open-tail $x $n)
   (if (== $n 0)
       (open-result $x)
       (let $dead (gc-payload $n)
         (gc-open-tail $x (- $n 1)))))

!(assertAlphaEqualToResult
  (gc-open-tail $who 80)
  ((open-result $same)))

(adv-row left alpha)
(adv-row right beta)

(= (gc-open-match-after-tail $n)
   (if (== $n 0)
       (match &self (adv-row $k $v) (out $k $v))
       (let $dead (gc-payload $n)
         (gc-open-match-after-tail (- $n 1)))))

!(assertEqualToResult
  (gc-open-match-after-tail 80)
  ((out left alpha) (out right beta)))

(= (gc-closed-collapse-after-tail $n)
   (if (== $n 0)
       (collapse (match &self (adv-row $k $v) (out $k $v)))
       (let $dead (gc-payload $n)
         (gc-closed-collapse-after-tail (- $n 1)))))

!(assertEqual
  (gc-closed-collapse-after-tail 80)
  ((out left alpha) (out right beta)))

!(bind! &gc-state (new-state 0))

(= (gc-state-loop $n)
   (if (== $n 0)
       (get-state &gc-state)
       (let $dead (gc-payload $n)
         (let $old (get-state &gc-state)
           (let $_ (change-state! &gc-state (+ $old 1))
             (gc-state-loop (- $n 1)))))))

!(assertEqual (gc-state-loop 40) 40)

(= (gc-table-source key) (table-value key))

(= (gc-table-loop $n)
   (if (== $n 0)
       (gc-table-source key)
       (let $dead (gc-payload $n)
         (gc-table-loop (- $n 1)))))

!(let* (
    (() (pragma! search-table-mode variant))
    (() (assertEqualToResult (gc-table-loop 80) ((table-value key))))
    (() (assertEqualToResult (gc-table-loop 80) ((table-value key))))
  )
  ())

(= (gc-survivor-loop $n)
   (if (== $n 0)
       (survivor escaped)
       (let $dead (gc-payload $n)
         (gc-survivor-loop (- $n 1)))))

!(bind! &gc-survivor (gc-survivor-loop 80))
!(assertEqual &gc-survivor (survivor escaped))
