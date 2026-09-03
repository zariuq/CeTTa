!(import! &self ../../../examples/atp/prime/search_modules.metta)
!(println! (ATPDirectExample checkpoint import))

!(bind! &direct-kb (new-space))

!(add-atom &direct-kb (: ph Type))
!(add-atom &direct-kb (: ps Type))
!(add-atom &direct-kb (: ch Type))
!(add-atom &direct-kb (: impl (-> Type Type Type)))
!(add-atom &direct-kb (: h1 ph))
!(add-atom &direct-kb (: h2 ps))
!(add-atom &direct-kb (: h3 (impl ph (impl ps ch))))
!(add-atom &direct-kb (: ax-mp (-> $a (impl $a $b) $b)))
!(add-atom &direct-kb (chaining-rule ax-mp))
!(println! (ATPDirectExample checkpoint mp-ready))

; Positive: the library discovers a proof term and the Prime checker accepts
; the term.  No ATP policy or unifier is installed in the core language.
!(assertEqual
   (atp:direct:first-checked &direct-kb ch 2 100000)
   (ATP.Checked (ax-mp h2 (ax-mp h1 h3)) ch (exact ch)))
!(println! (ATPDirectExample checkpoint mp-checked))

; Positive: arbitrary premise arity is represented by the arrow tail rather
; than by a separate unary/binary rule implementation.
!(add-atom &direct-kb (: A Type))
!(add-atom &direct-kb (: B Type))
!(add-atom &direct-kb (: C Type))
!(add-atom &direct-kb (: D Type))
!(add-atom &direct-kb (: pa A))
!(add-atom &direct-kb (: pb B))
!(add-atom &direct-kb (: pc C))
!(add-atom &direct-kb (: combine3 (-> A B C D)))
!(add-atom &direct-kb (chaining-rule combine3))
!(assertEqual
   (atp:direct:first-checked &direct-kb D 1 100000)
   (ATP.Checked (combine3 pa pb pc) D (exact D)))
!(println! (ATPDirectExample checkpoint arity-checked))

; Negative: absence of a derivation remains absence, never a refutation or a
; fabricated proof.
!(assertEqual (reify (atp:direct:prove &direct-kb Unprovable 0)) ())
!(println! (ATPDirectExample checkpoint absence-checked))

; Negative: the cyclic substitution that once fabricated an arbitrary target
; is rejected by ordinary MeTTa unification and again by check-type.
!(bind! &cyclic-kb (new-space))
!(add-atom &cyclic-kb (: Target Type))
!(add-atom &cyclic-kb (: neg (-> Type Type)))
!(add-atom &cyclic-kb (: impl (-> Type Type Type)))
!(add-atom &cyclic-kb (: pm2.24ii (-> $ph (neg $ph) $ps)))
!(add-atom &cyclic-kb (chaining-rule pm2.24ii))
!(add-atom &cyclic-kb (: pm2.18i (-> (impl (neg $ph) $ph) $ph)))
!(add-atom &cyclic-kb (chaining-rule pm2.18i))
!(add-atom &cyclic-kb (: notnotr (impl (neg (neg $ph)) $ph)))
!(add-atom &cyclic-kb (type-scheme notnotr))
!(add-atom &cyclic-kb (: notnot (impl $ph (neg (neg $ph)))))
!(add-atom &cyclic-kb (type-scheme notnot))
!(assertEqual (unify $x (neg $x) CyclicBindingAccepted CyclicBindingRejected)
              CyclicBindingRejected)
!(println! (ATPDirectExample checkpoint occurs-syntax-checked))
!(assertEqual
   (reify
     (atp:direct:certify
       (pm2.24ii (pm2.18i notnot) (pm2.18i notnotr))
       Target
       (check-type
         &cyclic-kb
         (pm2.24ii (pm2.18i notnot) (pm2.18i notnotr))
         Target
         100000)))
   ())
!(println! (ATPDirectExample summary 5 5 0))
