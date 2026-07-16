; DIFFERENTIAL (not a fidelity port): PeTTa (examples/types_nondet.metta) PRUNES
; (f T3in) to () when the output type clashes, then un-prunes after adding a
; matching type. Native HE does NOT prune: it reduces regardless, and duplicates
; per matching type. This test pins HE's actual behavior for the record.
(: Type1 Type) (: Type2 Type) (: Tdefault Type2)
(: f (-> Type1 Type1))
(: f (-> Type2 Type2))
(= (f $x) Tdefault)
(: T3in Type1)
!(assertEqual (f T3in) Tdefault)
(: T3in Type2)
; two type paths -> HE yields the reduct once per path (PeTTa would give one)
!(assertEqualToResult (f T3in) (Tdefault Tdefault))
