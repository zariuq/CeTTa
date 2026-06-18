; rhometta evidence revision: two agents compute evidence for the same
; hypothesis at COMM time; overlapping stamps are merged with
; Truth_OverlapCorrectedRevision (naive Truth_Revision double-counts).

!(import! &self rhometta)
!(import! &self lib_pln)
!(import! &self lib_overlap_corrected_revision)

; Observation slices.  Agents a1 and a2 BOTH observed r3 and r4 (overlap).
(obs a1 r1 black) (obs a1 r2 black) (obs a1 r3 black) (obs a1 r4 notblack)
(obs a2 r3 black) (obs a2 r4 notblack) (obs a2 r5 black) (obs a2 r6 black)
(stamp-of a1 (1 2 3 4))
(stamp-of a2 (3 4 5 6))

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; Evidence is computed, not looked up: count observations, build the stv.
(= (agent-evidence $a)
   (let* (($pos (TupleCount (collapse (match &self (obs $a $x black) $x))))
          ($neg (TupleCount (collapse (match &self (obs $a $x notblack) $x))))
          ($n (+ $pos $neg))
          ($stamp (match &self (stamp-of $a $s) $s)))
         (evi $a (stv (/ $pos $n) (/ $n (+ $n 1))) $stamp)))

; ---- Variant B: parallel fan-in, revise after the run -------------------
(= (revision-farm)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (agent-evidence a1)))
     (rho:recv (chan 1) $y1 (rho:drop $y1))
     (rho:send (chan 2) (rhometta:eval (agent-evidence a2)))
     (rho:recv (chan 2) $y2 (rho:drop $y2))))

(= (combine (rho:par (rho:val (evi $a $t1 $s1)) (rho:val (evi $b $t2 $s2))))
   (revised (Inheritance raven black)
            (overlap-corrected (Truth_OverlapCorrectedRevision $t1 $t2 $s1 $s2))
            (naive-double-counts (Truth_Revision $t1 $t2))))

; ---- Variant A: revision AT a rendezvous --------------------------------
; The received quoted names are referenced inside the next deferred payload;
; the revision itself is evaluated at the final COMM.
(= (combine2 (rho:quote (rho:val (evi $a $t1 $s1)))
             (rho:quote (rho:val (evi $b $t2 $s2))))
   (revised-at-rendezvous (Inheritance raven black)
      (Truth_OverlapCorrectedRevision $t1 $t2 $s1 $s2)))

(= (revision-chained)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (agent-evidence a1)))
     (rho:recv (chan 1) $e1
       (rho:par
         (rho:send (chan 2) (rhometta:eval (agent-evidence a2)))
         (rho:recv (chan 2) $e2
           (rho:send (chan 3) (rhometta:eval (combine2 $e1 $e2))))))
     (rho:recv (chan 3) $r (rho:drop $r))))

!("agent evidence, computed natively (reference):")
!(agent-evidence a1)
!(agent-evidence a2)

!("Variant B: parallel evidence farm, revision after the run:")
!(combine (rhometta:run (revision-farm)))

!("Variant A: revision computed AT the rendezvous (communication = computation):")
!(collapse (rhometta:run (revision-chained)))
