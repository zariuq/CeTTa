; ============================================================================
; ADVECTION DEMO --- Discrete propagation of evidence in WM-PLN
; ============================================================================
;
; Advection is the continuous-time generalization of Bellman / HJB:
;   forward continuity equation:  d rho/dt + div(rho * v) = 0
;   backward HJB equation:        d V/dt + max_a [r + grad V . f] = 0
;
; Discrete Bellman = time-discretization of HJB on a graph.
; Sum-product belief propagation = variable-elimination discretization
; of the forward continuity equation.
;
; PLN's local inference rules ARE discrete advection operators
; on the (strength, confidence) evidence carrier:
;
;   Truth_Deduction   --- transports (s, c) forward along a chain link
;                          (the velocity field for inheritance/implication)
;   Truth_Revision    --- merges two converging trajectories
;                          (the local "collision" / Bellman backup)
;   StampDisjoint     --- certifies that two trajectories did NOT share
;                          descent (the particles-don't-collide condition
;                          that makes additive merge sound)
;
; Three escalating rungs in one file:
;
;   Section A --- Linear chain A -> B -> C -> D
;                 Pure forward advection: one trajectory, no merge.
;                 The velocity field is Truth_Deduction; each step
;                 transports the leading edge's (s, c) by one link.
;
;   Section B --- Roman 4-node, two paths converging via Revision
;                 First interesting structural case: the network's
;                 symmetry produces two A -> D trajectories that
;                 converge on the same target atom. Revision is the
;                 local merge (the additive analog of Bellman's max_a).
;                 Mirrors hyperon/cetta-roman-mork-ffi/examples/
;                 bc_roman_4node.mm2 numerically.
;
;   Section C --- Multi-witness Revision under stamp-disjointness
;                 Particle advection: many witnesses carrying evidence,
;                 with NARS-style stamps preventing them from colliding
;                 (double-counting). StampDisjoint is the gate that
;                 keeps additive Revision sound.
;
; Run:  ./cetta examples/advection_demo.metta
; ============================================================================

!(import! &self ../lib/lib_wmpln)

; ============================================================================
; Section A --- Linear-chain advection (A -> B -> C -> D)
; ============================================================================
;
; All node priors:           (stv 0.5 0.9)
; All link truth-values:     (stv 0.8 0.9)
;
; Truth_Deduction strength formula (when Q is not near 1):
;   s_PR = s_PQ * s_QR + (1 - s_PQ) * (s_R - s_Q * s_QR) / (1 - s_Q)
;
; Step 1 (deduce A -> C from A -> B and B -> C):
;   s_AC = 0.8 * 0.8 + (1 - 0.8) * (0.5 - 0.5 * 0.8) / (1 - 0.5)
;        = 0.64 + 0.2 * 0.1 / 0.5
;        = 0.64 + 0.04
;        = 0.68
;   c_AC = min(0.9, 0.9, 0.9, 0.9, 0.9) = 0.9
;
; Step 2 (deduce A -> D from A -> C and C -> D, using step 1's result):
;   s_AD = 0.68 * 0.8 + (1 - 0.68) * (0.5 - 0.5 * 0.8) / (1 - 0.5)
;        = 0.544 + 0.32 * 0.2
;        = 0.544 + 0.064
;        = 0.608
;   c_AD = 0.9

!(println! "=== Section A: Linear-chain advection (A -> B -> C -> D) ===")

!(println! "Step 1 (A -> C, derived from A -> B and B -> C):")
!(println! (Truth_Deduction (stv 0.5 0.9)   ; node A prior
                            (stv 0.5 0.9)   ; node B prior
                            (stv 0.5 0.9)   ; node C prior
                            (stv 0.8 0.9)   ; link A -> B
                            (stv 0.8 0.9))) ; link B -> C
!(println! "  expected: (stv 0.68 0.9)")

!(println! "Step 2 (A -> D, derived from A -> C and C -> D):")
!(println! (Truth_Deduction (stv 0.5 0.9)   ; node A prior
                            (stv 0.5 0.9)   ; node C prior
                            (stv 0.5 0.9)   ; node D prior
                            (Truth_Deduction (stv 0.5 0.9)   ; recompute A -> C
                                             (stv 0.5 0.9)
                                             (stv 0.5 0.9)
                                             (stv 0.8 0.9)
                                             (stv 0.8 0.9))
                            (stv 0.8 0.9))) ; link C -> D
!(println! "  expected: (stv 0.608 0.9)")
!(println! "")

; ============================================================================
; Section B --- Roman 4-node advection-with-goal (two paths + Revision)
; ============================================================================
;
; KB (mirrors bc_roman_4node.mm2):
;   Node priors:  A(0.5, 0.9)  B(0.25, 0.9)  C(0.25, 0.9)  D(0.5, 0.9)
;   Links:        A -> B (stv 0.25 0.9)   B -> D (stv 0.5 0.9)
;                 A -> C (stv 0.25 0.9)   C -> D (stv 0.5 0.9)
;
; Each path computes A -> D by Truth_Deduction:
;   s_AD_viaB = 0.25 * 0.5 + (1 - 0.25) * (0.5 - 0.25 * 0.5) / (1 - 0.25)
;             = 0.125 + 0.75 * 0.375 / 0.75
;             = 0.125 + 0.375
;             = 0.5
;   By symmetry s_AD_viaC = 0.5 as well.
;
; Revision merges them:
;   w1 = c2w(0.9) = 0.9 / 0.1 = 9
;   w2 = c2w(0.9) = 9
;   w  = 18
;   revised strength   = (9 * 0.5 + 9 * 0.5) / 18 = 0.5
;   revised confidence = w2c(18) = 18 / 19 ~ 0.9474

!(println! "=== Section B: Roman 4-node advection-with-goal ===")

!(println! "Path via B (A -> D through A -> B -> D):")
!(println! (Truth_Deduction (stv 0.5 0.9)    ; A prior
                            (stv 0.25 0.9)   ; B prior
                            (stv 0.5 0.9)    ; D prior
                            (stv 0.25 0.9)   ; A -> B
                            (stv 0.5 0.9)))  ; B -> D
!(println! "  expected: (stv 0.5 0.9)")

!(println! "Path via C (A -> D through A -> C -> D):")
!(println! (Truth_Deduction (stv 0.5 0.9)    ; A prior
                            (stv 0.25 0.9)   ; C prior
                            (stv 0.5 0.9)    ; D prior
                            (stv 0.25 0.9)   ; A -> C
                            (stv 0.5 0.9)))  ; C -> D
!(println! "  expected: (stv 0.5 0.9)")

!(println! "Revision of both paths (the Bellman-style local backup):")
!(println! (Truth_Revision
             (Truth_Deduction (stv 0.5 0.9) (stv 0.25 0.9) (stv 0.5 0.9)
                              (stv 0.25 0.9) (stv 0.5 0.9))
             (Truth_Deduction (stv 0.5 0.9) (stv 0.25 0.9) (stv 0.5 0.9)
                              (stv 0.25 0.9) (stv 0.5 0.9))))
!(println! "  expected: (stv 0.5 0.9473684210526315)")
!(println! "")

; ============================================================================
; Section C --- Multi-witness Revision under stamp-disjointness
; ============================================================================
;
; Two independent witnesses for the same target atom:
;   witness 1: (stv 0.6 0.9) with stamp (1)
;   witness 2: (stv 0.4 0.8) with stamp (2)
;
; Revision arithmetic:
;   w1 = c2w(0.9) = 9
;   w2 = c2w(0.8) = 4
;   w  = 13
;   revised strength   = (9*0.6 + 4*0.4) / 13 = (5.4 + 1.6) / 13 = 7/13
;                       ~ 0.5385
;   revised confidence = w2c(13) = 13/14 ~ 0.9286
;
; StampDisjoint is the soundness gate:
;   (StampDisjoint (1)   (2))   = True   -> additive merge is sound
;   (StampDisjoint (1)   (1))   = False  -> would double-count
;   (StampDisjoint (1 2) (3 4)) = True   -> disjoint sets, sound
;   (StampDisjoint (1 2) (2 3)) = False  -> share element 2

!(println! "=== Section C: Multi-witness Revision under stamp-disjointness ===")

!(println! "Independent stamps (disjoint):")
!(println! ("  StampDisjoint (1) (2)     =" (StampDisjoint (1) (2))))
!(println! ("  StampDisjoint (1 2) (3 4) =" (StampDisjoint (1 2) (3 4))))

!(println! "Overlapping stamps (NOT disjoint):")
!(println! ("  StampDisjoint (1) (1)     =" (StampDisjoint (1) (1))))
!(println! ("  StampDisjoint (1 2) (2 3) =" (StampDisjoint (1 2) (2 3))))

!(println! "Stamped revision arithmetic:")
!(println! ("  Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8) ="
            (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8))))
!(println! "  expected: (stv ~0.5385 ~0.9286)")

!(println! "Stamp aggregation under StampConcat (sorts and merges):")
!(println! ("  StampConcat (1) (2)     =" (StampConcat (1) (2))))
!(println! ("  StampConcat (1 3) (2 4) =" (StampConcat (1 3) (2 4))))

; Boolean-valued assertions (exact, no float concerns)
!(assertEqual (StampDisjoint (1) (2)) True)
!(assertEqual (StampDisjoint (1) (1)) False)
!(assertEqual (StampDisjoint (1 2) (3 4)) True)
!(assertEqual (StampDisjoint (1 2) (2 3)) False)

!(println! "")
!(println! "=== ADVECTION DEMO COMPLETE ===")
