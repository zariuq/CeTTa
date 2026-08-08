; Independent Prime replay for the two proof occurrences emitted by Nil
; Geisweiller's bounded backward-via-forward Jarr search.
;
; Nil's continuation-coded I/mpi terms are beta-reduced mechanically to this
; ordinary Hilbert vocabulary.  Replay does no search: axiom schemas and modus
; ponens are the whole trusted presentation consumed by check-type.

(: p Type)
(: q Type)
(: r Type)
(: imp (-> Type Type Type))
(: neg (-> Type Type))

(: ax1-explicit
   (-> (: $ph Type) (: $ps Type)
       (imp $ph (imp $ps $ph))))

(: ax2-explicit
   (-> (: $ph Type) (: $ps Type) (: $ch Type)
       (imp
         (imp $ph (imp $ps $ch))
         (imp (imp $ph $ps) (imp $ph $ch)))))

(: ax3-explicit
   (-> (: $ph Type) (: $ps Type)
       (imp
         (imp (neg $ph) (neg $ps))
         (imp $ps $ph))))

(: mp (-> (imp $ph $ps) $ph $ps))
(chaining-rule mp)

!(check-type
   &self
   (mp
     (mp
       (ax2-explicit
         (imp (imp p q) r)
         (imp q (imp (imp p q) r))
         (imp q r))
       (mp
         (ax1-explicit
           (imp (imp q (imp (imp p q) r)) (imp q r))
           (imp (imp p q) r))
         (mp
           (mp
             (ax2-explicit
               (imp q (imp (imp p q) r))
               (imp q (imp p q))
               (imp q r))
             (ax2-explicit q (imp p q) r))
           (mp
             (ax1-explicit
               (imp q (imp p q))
               (imp q (imp (imp p q) r)))
             (ax1-explicit q p)))))
     (ax1-explicit (imp (imp p q) r) q))
   (imp (imp (imp p q) r) (imp q r))
   100000000)

!(check-type
   &self
   (mp
     (mp
       (ax2-explicit
         (imp (imp p q) r)
         (imp q (imp p q))
         (imp q r))
       (mp
         (mp
           (ax2-explicit
             (imp (imp p q) r)
             (imp q (imp (imp p q) r))
             (imp (imp q (imp p q)) (imp q r)))
           (mp
             (ax1-explicit
               (imp
                 (imp q (imp (imp p q) r))
                 (imp (imp q (imp p q)) (imp q r)))
               (imp (imp p q) r))
             (ax2-explicit q (imp p q) r)))
         (ax1-explicit (imp (imp p q) r) q)))
     (mp
       (ax1-explicit (imp q (imp p q)) (imp (imp p q) r))
       (ax1-explicit q p)))
   (imp (imp (imp p q) r) (imp q r))
   100000000)

; Negative canary: changing one ax2 occurrence to ax3 must not replay as Jarr.
!(check-type
   &self
   (mp
     (mp
       (ax3-explicit p q)
       (ax1-explicit p q))
     (ax1-explicit p q))
   (imp (imp (imp p q) r) (imp q r))
   100000000)
