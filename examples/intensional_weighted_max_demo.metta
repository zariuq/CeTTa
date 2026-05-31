; ============================================================================
; INTENSIONAL WEIGHTED DEMO --- community-trait reasoning
; ============================================================================
;
; A worked example of §14 Concrete Intensional Inheritance using the
; community-trait use case Max mentioned (inferring traits about people from
; behavioral observations).
;
; Trait modeled:  "careful"
; Properties:
;   double-checks-work     (weight 1.0)
;   reads-instructions     (weight 0.7)
;   asks-for-clarification (weight 0.5)
;   shows-up-on-time       (weight 0.3)
;
; Two entities:
;   alice : strong fit for the trait
;   bob   : weak/partial fit
;
; What this demonstrates:
;   - The weighted aggregation of property evidence
;   - Audit output with $covered (what we found) and $missing (what we lack)
;   - Difference between an entity with strong evidence and one with weak
;
; Run:  cd /home/zar/claude/hyperon/CeTTa
;       ./cetta examples/intensional_weighted_max_demo.metta
; ============================================================================

!(import! &self ../lib/lib_wmpln)
!(import! &self ../lib/lib_overlap_corrected_revision)
!(import! &self ../lib/lib_intensional_weighted)

!(bind! &kb (new-space))

;; Node priors --- conservative balanced priors for unknown nodes.
(= (STV alice) (stv 0.1 0.9))
(= (STV bob)   (stv 0.1 0.9))
(= (STV (IntSet double-checks-work))     (stv 0.3 0.9))
(= (STV (IntSet reads-instructions))     (stv 0.3 0.9))
(= (STV (IntSet asks-for-clarification)) (stv 0.3 0.9))
(= (STV (IntSet shows-up-on-time))       (stv 0.3 0.9))

;; Concept schema: "careful" is characterized by four weighted properties.
!(add-atom &kb (ConceptProp careful double-checks-work     1.0))
!(add-atom &kb (ConceptProp careful reads-instructions     0.7))
!(add-atom &kb (ConceptProp careful asks-for-clarification 0.5))
!(add-atom &kb (ConceptProp careful shows-up-on-time       0.3))

;; Knowledge base: observations of alice and bob.
;;
;; Stamps mark independent observation events.  Different stamps for
;; different observations on the same person are CORRECT (independent
;; sightings).  Reusing the same stamp across properties would be the
;; "I saw one situation that confirmed multiple traits" case --- handled
;; by the overlap-corrected revision.
;;
;; Added to &kb so the intensional layer can query an explicit working KB.
!(add-atom &kb (Sentence ((Inheritance alice (IntSet double-checks-work))     (stv 0.9 0.9)) (10)))
!(add-atom &kb (Sentence ((Inheritance alice (IntSet reads-instructions))     (stv 0.85 0.9)) (11)))
!(add-atom &kb (Sentence ((Inheritance alice (IntSet asks-for-clarification)) (stv 0.7 0.85)) (12)))
!(add-atom &kb (Sentence ((Inheritance alice (IntSet shows-up-on-time))       (stv 0.8 0.9)) (13)))

!(add-atom &kb (Sentence ((Inheritance bob (IntSet double-checks-work)) (stv 0.3 0.8)) (20)))
!(add-atom &kb (Sentence ((Inheritance bob (IntSet reads-instructions)) (stv 0.4 0.7)) (21)))
;; bob has no observation for asks-for-clarification (missing)
!(add-atom &kb (Sentence ((Inheritance bob (IntSet shows-up-on-time))   (stv 0.6 0.7)) (23)))

(= (alice-careful-stv)
   (IntensionalMemberSTV &kb alice careful 30))

(= (alice-careful-audit)
   (IntensionalMemberAudit &kb alice careful 30))

(= (bob-careful-stv)
   (IntensionalMemberSTV &kb bob careful 30))

(= (bob-careful-audit)
   (IntensionalMemberAudit &kb bob careful 30))

;; -----------------------------------------------------------------------
;; alice: should score HIGH for "careful"
;; -----------------------------------------------------------------------

!(println! "==========================================================")
!(println! "INTENSIONAL WEIGHTED DEMO --- Is X careful?")
!(println! "==========================================================")
!(println! "")
!(println! "Alice --- four observed careful-ish behaviors, mostly positive:")
!(println! "")

!(println! "  IntensionalMemberSTV (alice, careful):")
!(println! ("    " (alice-careful-stv)))
!(println! "")

!(println! "  IntensionalMemberAudit (alice, careful):")
!(println! ("    " (alice-careful-audit)))
!(println! "")

;; -----------------------------------------------------------------------
;; bob: should score LOW for "careful", with one MISSING property
;; -----------------------------------------------------------------------

!(println! "Bob --- three observed behaviors (mixed), one property unobserved:")
!(println! "")

!(println! "  IntensionalMemberSTV (bob, careful):")
!(println! ("    " (bob-careful-stv)))
!(println! "")

!(println! "  IntensionalMemberAudit (bob, careful):")
!(println! ("    " (bob-careful-audit)))
!(println! "")

!(println! "==========================================================")
!(println! "Honesty interpretation:")
!(println! "  - alice's $covered has all four properties --- full evidence.")
!(println! "  - bob's $missing includes asks-for-clarification --- a property")
!(println! "    the concept demands but we have NO observation for.  An")
!(println! "    honest agent would surface this rather than silently treat")
!(println! "    'not seen' as 'not satisfied'.")
!(println! "  - the merged confidence reflects accumulated observations,")
!(println! "    discounted by stamp-overlap (if any).  For independent")
!(println! "    sightings (different stamps per property), no overlap")
!(println! "    correction fires --- evidence accumulates monotonically.")
!(println! "==========================================================")
