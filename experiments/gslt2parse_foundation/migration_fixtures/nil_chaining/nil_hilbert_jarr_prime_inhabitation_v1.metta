; The Jarr theorem from Nil Geisweiller's backward-via-forward experiment,
; presented as ordinary Prime type inhabitation.  The Hilbert axiom schemas
; are polymorphic proof constants; modus ponens is the only chaining rule.
;
; This fixture deliberately keeps proof search and proof checking separate:
; search-inhabitants proposes terms, and Prime's ordinary typing boundary
; checks every reported inhabitant.

(: p Type)
(: q Type)
(: r Type)
(: imp (-> Type Type Type))
(: neg (-> Type Type))

(: ax1 (imp $ph (imp $ps $ph)))
(type-scheme ax1)

(: ax2
   (imp
     (imp $ph (imp $ps $ch))
     (imp (imp $ph $ps) (imp $ph $ch))))
(type-scheme ax2)

(: ax3
   (imp
     (imp (neg $ph) (neg $ps))
     (imp $ps $ph)))
(type-scheme ax3)

(: mp (-> (imp $ph $ps) $ph $ps))
(chaining-rule mp)

!(search-inhabitants
   &self
   (imp (imp (imp p q) r) (imp q r))
   6
   100000000
   2
   (search-policy atp-guided-inhabitation))
