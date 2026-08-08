; Typed forward-inhabitation form of Nil Geisweiller's bounded
; backward-via-forward Jarr experiment.
;
; The target appears only in the concrete open-identity seed.  The four rule
; schemas below are the reusable Hilbert transition system: inverse modus
; ponens and the three axiom discharges.  Prime's ordinary typed forward
; closure produces candidate declarations; depth-zero inhabitation then reads
; the closed occurrences and rechecks them through the typing boundary.

(: p Type)
(: q Type)
(: r Type)
(: imp (-> Type Type Type))
(: neg (-> Type Type))

(: HNil Type)
(: HCons (-> Type Type Type))
(: Open (-> Type Type Type))

(: I
   (Open
     (HCons
       (imp (imp (imp p q) r) (imp q r))
       HNil)
     (imp (imp (imp p q) r) (imp q r))))

(: open-mpi
   (->
     (Open (HCons $b $rest) $c)
     (Open (HCons (imp $a $b) (HCons $a $rest)) $c)))
(chaining-rule open-mpi)

(: open-ax1
   (->
     (Open (HCons (imp $ph (imp $ps $ph)) $rest) $c)
     (Open $rest $c)))
(chaining-rule open-ax1)

(: open-ax2
   (->
     (Open
       (HCons
         (imp
           (imp $ph (imp $ps $ch))
           (imp (imp $ph $ps) (imp $ph $ch)))
         $rest)
       $c)
     (Open $rest $c)))
(chaining-rule open-ax2)

(: open-ax3
   (->
     (Open
       (HCons
         (imp (imp (neg $ph) (neg $ps)) (imp $ps $ph))
         $rest)
       $c)
     (Open $rest $c)))
(chaining-rule open-ax3)

!(type-forward-closure &self 13 100000000 2000000)

!(search-inhabitants
   &self
   (Open HNil (imp (imp (imp p q) r) (imp q r)))
   0 100000000 2)
