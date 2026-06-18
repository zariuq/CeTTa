!(import! &self rhometta)
!(import! &self lib_pln)
!(import! &self lib_overlap_corrected_revision)

(obs a1 r1 black) (obs a1 r2 black) (obs a1 r3 black) (obs a1 r4 notblack)
(obs a2 r3 black) (obs a2 r4 notblack) (obs a2 r5 black) (obs a2 r6 black)
(stamp-of a1 (1 2 3 4))
(stamp-of a2 (3 4 5 6))

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

(= (agent-evidence $a)
   (let* (($pos (TupleCount (collapse (match &self (obs $a $x black) $x))))
          ($neg (TupleCount (collapse (match &self (obs $a $x notblack) $x))))
          ($n (+ $pos $neg))
          ($stamp (match &self (stamp-of $a $s) $s)))
         (evi $a (stv (/ $pos $n) (/ $n (+ $n 1))) $stamp)))

(= (revision-farm)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (agent-evidence a1)))
     (rho:recv (chan 1) $y1 (rho:drop $y1))
     (rho:send (chan 2) (rhometta:eval (agent-evidence a2)))
     (rho:recv (chan 2) $y2 (rho:drop $y2))))

(= (combine (rho:par (rho:val (evi $a $t1 $s1)) (rho:val (evi $b $t2 $s2))))
   (revised (overlap-corrected (Truth_OverlapCorrectedRevision $t1 $t2 $s1 $s2))
            (naive (Truth_Revision $t1 $t2))))

(= (evi-tv (evi $a $t $s)) $t)

; Variant B: post-run combination equals the natively-computed revision.
!(assertEqual
  (combine (rhometta:run (revision-farm)))
  (revised (overlap-corrected
             (Truth_OverlapCorrectedRevision (evi-tv (agent-evidence a1))
                                             (evi-tv (agent-evidence a2))
                                             (1 2 3 4) (3 4 5 6)))
           (naive (Truth_Revision (evi-tv (agent-evidence a1))
                                  (evi-tv (agent-evidence a2))))))

; Variant A: revision computed AT the rendezvous (binder referenced inside a
; later deferred payload) equals the same natively-computed combination.
(= (combine2 (rho:quote (rho:val (evi $a $t1 $s1)))
             (rho:quote (rho:val (evi $b $t2 $s2))))
   (revised-at-rendezvous (Truth_OverlapCorrectedRevision $t1 $t2 $s1 $s2)))

(= (revision-chained)
   (rho:par
     (rho:send (chan 1) (rhometta:eval (agent-evidence a1)))
     (rho:recv (chan 1) $e1
       (rho:par
         (rho:send (chan 2) (rhometta:eval (agent-evidence a2)))
         (rho:recv (chan 2) $e2
           (rho:send (chan 3) (rhometta:eval (combine2 $e1 $e2))))))
     (rho:recv (chan 3) $r (rho:drop $r))))

!(assertEqual
  (collapse (rhometta:run (revision-chained)))
  (collapse (rho:val (combine2 (rho:quote (rho:val (agent-evidence a1)))
                               (rho:quote (rho:val (agent-evidence a2)))))))
