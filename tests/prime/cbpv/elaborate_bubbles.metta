; The demand-bubble fixture tests/prime/demand/bubbles.metta, elaborated into
; ContextualCBPV and run by lib/prime/cbpv.metta, against what the draft gives
; for it (tests/prime/demand/bubbles.expected, whose answers and report
; records are copied below).  The program is written with inert heads
; (s:let, s:demand for demand:committed, s:licensed for demand:licensed, s:+)
; and variables (V name), so no part of it runs as code while it is read.
; A licensed bubble is elaborated by the agreement conditions themselves: the
; elaborator counts the uses of each bound value and the answers of its
; producer and applies the strategy only where they agree.
;   (Row label agrees elaborated observed): the answers, as sorted bags.
;   (Decisions label agrees mine theirs): one record per binding site the
;   bubble changes, (argument POSITION HEAD VERDICT) or (binding VERDICT).
; The cyclic names loop-a and loop-b need recursion, which is not
; elaborated.
!(import! &self ../../../lib/prime/cbpv.metta)

(cbpv:def-symbol coin heads)
(cbpv:def-symbol coin tails)
(cbpv:def k ((V x) (V y)) (V x))
(cbpv:def first ((Cons (V h) (V t))) (V h))
(cbpv:def flip () (Cons heads Nil))
(cbpv:def flip () (Cons tails Nil))
(cbpv:def die () 1)
(cbpv:def die () 2)
(cbpv:def twice ((V x)) (P (V x) (V x)))

(= (row $label (quote $e) $observed)
   (let $mine (cbpv:elaborated-bag (quote $e))
     (let $theirs (sort-atom $observed)
       (Row $label (== $mine $theirs) $mine $theirs))))

; A draft report record (demand-applied|demand-kept|demand-changed S REASON
; SITE) as a decision.
(= (verdict-of demand-applied) applied)
(= (verdict-of demand-kept) kept)
(= (verdict-of demand-changed) changed)
(= (draft-decision $record)
   (let ($kind $strategy $reason $site) $record
     (let $verdict (verdict-of $kind)
       (let $tag (car-atom $site)
         (if (== $tag argument)
             (let ($a $position $call) $site
               (let $head (car-atom $call) (argument $position $head $verdict)))
             (binding $verdict))))))
(= (draft-decisions $records)
   (if (== $records ()) ()
       (let $first (car-atom $records)
         (let $rest (cdr-atom $records)
           (let $d (draft-decision $first)
             (let $ds (draft-decisions $rest) (cons-atom $d $ds)))))))
(= (plain-head $h) (if (== $h s:+) + (if (== $h s:-) - (if (== $h s:*) * $h))))
(= (plain-decisions $records)
   (if (== $records ()) ()
       (let $first (car-atom $records)
         (let $rest (cdr-atom $records)
           (let $d (plain-decision $first)
             (let $ds (plain-decisions $rest) (cons-atom $d $ds)))))))
(= (plain-decision $d)
   (let $tag (car-atom $d)
     (if (== $tag argument)
         (let ($a $position $head $verdict) $d
           (let $plain (plain-head $head) (argument $position $plain $verdict)))
         $d)))
(= (decisions $label (quote $e) (quote $records))
   (let $report (cbpv:license-report (quote $e))
     (let $mine (plain-decisions $report)
       (let $theirs (draft-decisions $records)
         (let $a (sort-atom $mine)
           (let $b (sort-atom $theirs)
             (Decisions $label (== $a $b) $a $b)))))))

; ---- discarding
!(row discard-default (quote (k 1 coin)) (1))
!(row discard-eager (quote (s:demand eager (k 1 coin))) (1 1))
!(row discard-licensed (quote (s:licensed eager (k 1 coin))) (1))
!(decisions discard-eager (quote (s:demand eager (k 1 coin)))
   (quote ((demand-changed eager (discarded-contributes-answers) (argument 2 (k 1 coin))))))
!(decisions discard-licensed (quote (s:licensed eager (k 1 coin)))
   (quote ((demand-kept lazy (may-discard-answers) (argument 2 (k 1 coin))))))
!(row demanded-licensed (quote (s:licensed eager (first (flip)))) (heads tails))
!(decisions demanded-licensed (quote (s:licensed eager (first (flip))))
   (quote ((demand-applied eager (demanded GSLT.Dynamics.DemandAgreement.eagerShared_eq_lazyShared_iff)
             (argument 1 (first (flip)))))))
!(row plus-licensed (quote (s:licensed eager (s:+ (k 1 coin) 1))) (2))
!(decisions plus-licensed (quote (s:licensed eager (s:+ (k 1 coin) 1)))
   (quote ((demand-kept lazy (may-discard-answers) (argument 2 (k 1 coin)))
           (demand-applied eager (demanded GSLT.Dynamics.DemandAgreement.eagerShared_eq_lazyShared_iff)
             (argument 1 (+ (k 1 coin) 1))))))
!(row plus-eager (quote (s:demand eager (s:+ (k 1 coin) 1))) (2 2))
!(decisions plus-eager (quote (s:demand eager (s:+ (k 1 coin) 1)))
   (quote ((demand-changed eager (discarded-contributes-answers) (argument 2 (k 1 coin)))
           (demand-applied eager (demanded GSLT.Dynamics.DemandAgreement.eagerShared_eq_lazyShared_iff)
             (argument 1 (+ (k 1 coin) 1))))))
!(row name-once-eager (quote (s:demand eager (k (k 1 coin) coin))) (1 1))
; The draft reports the first argument of the outer k, which k returns, as a
; change that may discard answers.  k uses that argument once, so eager and
; lazy agree there (n = 1); the elaboration reports it applied.  The answers
; are the same either way.
!(decisions name-once-eager (quote (s:demand eager (k (k 1 coin) coin)))
   (quote ((demand-changed eager (discarded-contributes-answers) (argument 2 (k 1 coin)))
           (demand-changed eager (may-discard-answers) (argument 1 (k (k 1 coin) coin)))
           (demand-changed eager (discarded-contributes-answers) (argument 2 (k (k 1 coin) coin))))))

; ---- copying
!(row let-shared (quote (s:let (V c) coin (P (V c) (V c)))) ((P heads heads) (P tails tails)))
!(row let-resample (quote (s:demand resample (s:let (V c) coin (P (V c) (V c)))))
     ((P heads heads) (P heads tails) (P tails heads) (P tails tails)))
!(decisions let-resample (quote (s:demand resample (s:let (V c) coin (P (V c) (V c)))))
   (quote ((demand-changed resample (copied-per-use) (binding $c coin)))))
!(row let-resample-licensed (quote (s:licensed resample (s:let (V c) coin (P (V c) (V c)))))
     ((P heads heads) (P tails tails)))
!(decisions let-resample-licensed (quote (s:licensed resample (s:let (V c) coin (P (V c) (V c)))))
   (quote ((demand-kept eager (may-copy-answers) (binding $c coin)))))
!(row let-used-once-licensed (quote (s:licensed resample (s:let (V d) (die) (s:+ (V d) 10)))) (11 12))
!(decisions let-used-once-licensed (quote (s:licensed resample (s:let (V d) (die) (s:+ (V d) 10))))
   (quote ((demand-applied resample (used-once GSLT.Dynamics.DemandAgreement.strategies_agree_iff)
             (binding $d (die))))))

; ---- laziness for a let
!(row let-unused-eager (quote (s:let (V c) coin done)) (done done))
!(row let-unused-lazy (quote (s:demand lazy (s:let (V c) coin done))) (done))
!(decisions let-unused-lazy (quote (s:demand lazy (s:let (V c) coin done)))
   (quote ((demand-changed lazy (unused-unrun) (binding $c coin)))))
!(row let-unused-lazy-licensed (quote (s:licensed lazy (s:let (V c) coin done))) (done done))
!(decisions let-unused-lazy-licensed (quote (s:licensed lazy (s:let (V c) coin done)))
   (quote ((demand-kept eager (may-discard-answers) (binding $c coin)))))
!(row let-used-lazy-licensed (quote (s:licensed lazy (s:let (V d) (die) (s:+ (V d) 10)))) (11 12))
!(decisions let-used-lazy-licensed (quote (s:licensed lazy (s:let (V d) (die) (s:+ (V d) 10))))
   (quote ((demand-applied lazy (used GSLT.Dynamics.DemandAgreement.eagerShared_eq_lazyShared_iff)
             (binding $d (die))))))

; ---- an argument passed by call is one shared suspension
!(row argument-resample-refused (quote (s:demand resample (twice coin)))
     ((P heads heads) (P tails tails)))
!(decisions argument-resample-refused (quote (s:demand resample (twice coin)))
   (quote ((demand-kept lazy (by-name-unavailable) (argument 1 (twice coin))))))

; ---- a bubble with a strategy the draft does not know stays as written
!(row unknown-strategy (quote (s:demand sideways (k 1 coin))) ((demand:committed sideways (k 1 coin))))
