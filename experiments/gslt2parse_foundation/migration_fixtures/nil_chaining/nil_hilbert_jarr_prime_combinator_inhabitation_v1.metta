; The Jarr theorem from Nil Geisweiller's backward-via-forward experiment,
; expressed as Prime type inhabitation over the experiment's open-proof
; transition system.  Unlike the direct modus-ponens presentation, this makes
; the intermediate hypothesis stack explicit and therefore preserves the
; constraint that guides Nil's inverse proof search.

!(import! &self lib_atp)

(: p Type)
(: q Type)
(: r Type)
(: imp (-> Type Type Type))
(: neg (-> Type Type))

(: HNil Type)
(: HCons (-> Type Type Type))
(: Open (-> Type Type Type))

; An open proof starts with one assumption proving itself.
(: open-I (Open (HCons $a HNil) $a))
(type-scheme open-I)

; Inverse modus ponens replaces the leading b assumption by a->b and a.
(: open-mpi
   (->
     (Open (HCons $b $rest) $c)
     (Open (HCons (imp $a $b) (HCons $a $rest)) $c)))
(chaining-rule open-mpi)

; Applying a Hilbert axiom discharges the leading matching assumption.
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

; The index is a search hint only.  Its entries name authored declarations;
; every completed term is still checked against the typing space above.
!(bind! &jarr-open-index (new-space))
!(add-atom &jarr-open-index (ATP.Index.Fact Open 0 open-I))
!(add-atom &jarr-open-index (ATP.Index.Rule Open 1 open-mpi))
!(add-atom &jarr-open-index (ATP.Index.Rule Open 2 open-ax1))
!(add-atom &jarr-open-index (ATP.Index.Rule Open 3 open-ax2))
!(add-atom &jarr-open-index (ATP.Index.Rule Open 4 open-ax3))

!(bind! &jarr-open-policy
  (ATP.AgendaSearch
    (ATP.AgendaPolicy 16 64 4096 1 1 0 4096 32)
    (ATP.DirectSearch
      (ATP.TopK 16 (ATP.LinearPolicy 4096 2048 512 256 8 0 4))
      ATP.RightToLeft)))

; A mechanical translation of Nil's first known proof occurrence.  This is a
; checker canary, not a search hint: the agenda below never consumes the term.
!(check-type
   &self open-I
   (Open (HCons p HNil) p)
   100000000)

!(check-type
   &self (open-mpi open-I)
   (Open (HCons (imp p q) (HCons p HNil)) q)
   100000000)

!(check-type
   &self
   (open-ax1
     (open-ax1
       (open-ax1
         (open-mpi
           (open-ax2
             (open-ax2
               (open-mpi
                 (open-mpi
                   (open-ax1
                     (open-mpi
                       (open-ax2
                         (open-mpi
                           (open-mpi open-I)))))))))))))
   (Open HNil (imp (imp (imp p q) r) (imp q r)))
   100000000)

!(atp:agenda:first-checked
   &self &jarr-open-index
   (Open HNil (imp (imp (imp p q) r) (imp q r)))
   13 0 100000000 &jarr-open-policy)
