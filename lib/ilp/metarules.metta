; First-class Metagol-style metarules as MeTTa data.
;
; The representation keeps the two variable strata explicit:
;   (so P) -- second-order predicate/program variable
;   (fo A) -- first-order object variable
; This is the characteristic meta-interpretive-learning bias: metarules quantify
; over predicate symbols while ordinary clause variables range over objects.
;
; These records are inspectable/specification data.  The executable learner uses
; the corresponding typed relation combinators in metagol_core.metta.

(= (mil:metarule identity)
   (MIL.MetaRule identity
     ((so P) (so Q) (fo A) (fo B))
     (MIL.Atom (so P) (fo A) (fo B))
     ((MIL.Atom (so Q) (fo A) (fo B)))))

(= (mil:metarule inverse)
   (MIL.MetaRule inverse
     ((so P) (so Q) (fo A) (fo B))
     (MIL.Atom (so P) (fo A) (fo B))
     ((MIL.Atom (so Q) (fo B) (fo A)))))

(= (mil:metarule chain)
   (MIL.MetaRule chain
     ((so P) (so Q) (so R) (fo A) (fo B) (fo C))
     (MIL.Atom (so P) (fo A) (fo B))
     ((MIL.Atom (so Q) (fo A) (fo C))
      (MIL.Atom (so R) (fo C) (fo B)))))

(= (mil:metarule tailrec)
   (MIL.MetaRule tailrec
     ((so P) (so Q) (fo A) (fo B) (fo C))
     (MIL.Atom (so P) (fo A) (fo B))
     ((MIL.Atom (so Q) (fo A) (fo C))
      (MIL.Atom (so P) (fo C) (fo B)))))

; Higher-order interpreted background knowledge: F is a second-order argument
; passed through map.  The map recursion itself is background knowledge; the
; learner induces F (and can induce a sub-program for it).
(= (mil:metarule map-ibk)
   (MIL.MetaRuleHO map-ibk
     ((so P) (so F) (fo As) (fo Bs))
     (MIL.Atom (so P) (fo As) (fo Bs))
     (MIL.InterpretedCall MIL.MapOp (so F) (fo As) (fo Bs))))

; Correspondence from common metarule instances to the compact executable
; hypothesis language.  This is intentionally a producer-side compilation;
; example/type checking remains authoritative.
(= (mil:compile-meta-instance (MIL.MetaInstance identity $q))
   (MIL.Id $q))
(= (mil:compile-meta-instance (MIL.MetaInstance chain $q $r))
   (MIL.Chain $q $r))
(= (mil:compile-meta-instance (MIL.MetaInstance tailrec $q))
   (MIL.Closure $q))
(= (mil:compile-meta-instance (MIL.MetaInstance map-ibk $f))
   (MIL.HO1 MIL.MapOp $f))
