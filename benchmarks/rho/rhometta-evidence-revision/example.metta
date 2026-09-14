; rhometta-evidence-revision -- the ONE place a truth value is genuinely COMPUTED.
;
; Two agents each observe some ravens and form an opinion on the hypothesis
; (Inheritance raven black) -- PLN for ``ravens are black'' -- with strength =
; fraction seen black and confidence = how much evidence backs it.  They share
; two observations (r3, r4).  Merging their opinions NAIVELY double-counts that shared
; evidence and reports inflated confidence; overlap-corrected revision discounts
; the shared stamps and reports the honest confidence.  Same strength either way
; (0.75); the honest difference is the confidence: 0.889 naive vs 0.857 corrected.
;
; Here the truth value is COUNTED FROM OBSERVATIONS, not hand-set -- which is why
; it belongs on a probabilistic judgment, unlike the crisp proof terms in the
; other rho demos (a typing/derivation is true or it is not).
;
; Run:  ./cetta --quiet --profile extended --lang he <this file>

!(import! &self rhometta)
!(import! &self lib_pln)
!(import! &self lib_overlap_corrected_revision)

; Standard rho notation used below:
;   P | Q                 parallel composition
;   x!(m)                send process m on channel x
;   for (y <- x) { P }  receive from x, bind y in P
;   *y                   drop: run the quoted process/name y

; Observation slices.  Agents a1 and a2 BOTH observed r3 and r4 (overlap).
(obs a1 r1 black) (obs a1 r2 black) (obs a1 r3 black) (obs a1 r4 notblack)
(obs a2 r3 black) (obs a2 r4 notblack) (obs a2 r5 black) (obs a2 r6 black)
(stamp-of a1 (1 2 3 4))
(stamp-of a2 (3 4 5 6))

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; readable syntax: round for display, name the two numbers, drop the rho:val carrier.
(= (round3 $x) (/ (round-math (* $x 1000)) 1000))
(= (sc (stv $s $c)) (strength (round3 $s) confidence (round3 $c)))
(= (rev-of (rho:val $x)) $x)

; Evidence is computed, not looked up: count observations, build the stv.
(= (agent-evidence $a)
   (let* (($pos (TupleCount (collapse (match &self (obs $a $x black) $x))))
          ($neg (TupleCount (collapse (match &self (obs $a $x notblack) $x))))
          ($n (+ $pos $neg))
          ($stamp (match &self (stamp-of $a $s) $s)))
         (evi $a (stv (/ $pos $n) (/ $n (+ $n 1))) $stamp)))

; ---- Variant B: parallel fan-in, revise after the run -------------------
; Standard rho shape:
;   c1!(eval(agent-evidence a1)) | for (y1 <- c1) { *y1 }
; | c2!(eval(agent-evidence a2)) | for (y2 <- c2) { *y2 }
; The two opinions are computed independently in parallel; the revision formula
; is then applied afterward by plain MeTTa code to the quiescent result.
(= (revision-farm)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (agent-evidence a1)))
     (rho:recv (chan 1) $y1 (rho:drop $y1))
     (rho:send (chan 2) (rhometta:eval (agent-evidence a2)))
     (rho:recv (chan 2) $y2 (rho:drop $y2))))

(= (combine (rho:par (rho:val (evi $a $t1 $s1)) (rho:val (evi $b $t2 $s2))))
   (revised (Inheritance raven black)
            (overlap-corrected (sc (Truth_OverlapCorrectedRevision $t1 $t2 $s1 $s2)))
            (naive-double-counts (sc (Truth_Revision $t1 $t2)))))

; ---- Variant A: revision AT a rendezvous --------------------------------
; The received quoted names are referenced inside the next deferred payload;
; the revision itself is evaluated at the final COMM.
; Standard rho shape:
;   c1!(eval(E1)) | for (e1 <- c1) {
;     c2!(eval(E2)) | for (e2 <- c2) {
;       c3!(eval(combine2 e1 e2))
;     }
;   } | for (r <- c3) { *r }
; Here the revision is not "after the run"; it is the payload of the final send.
(= (combine2 (rho:quote (rho:val (evi $a $t1 $s1)))
             (rho:quote (rho:val (evi $b $t2 $s2))))
   (revised-at-rendezvous (Inheritance raven black)
      (sc (Truth_OverlapCorrectedRevision $t1 $t2 $s1 $s2))))

(= (revision-chained)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (agent-evidence a1)))
     (rho:recv (chan 1) $e1
       (rho:par
         (rho:send (chan 2) (rhometta:eval (agent-evidence a2)))
         (rho:recv (chan 2) $e2
           (rho:send (chan 3) (rhometta:eval (combine2 $e1 $e2))))))
     (rho:recv (chan 3) $r (rho:drop $r))))

!("each agent's opinion, computed from its own observations (strength, confidence, stamp):")
!(agent-evidence a1)
!(agent-evidence a2)

!("merge them -- overlap-corrected vs naive double-counting (Variant B, after the run):")
!(combine (rhometta:run (revision-farm)))

!("the same revision, computed AT the final rendezvous (Variant A):")
!(collapse (rev-of (rhometta:run (revision-chained))))
