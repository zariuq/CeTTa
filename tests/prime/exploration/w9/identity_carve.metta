; The identity carve, under the uip and univalence profiles.  The h-set
; fragment is exactly the part of identity that validates uniqueness of
; identity proofs; a universe whose identifications are equivalences lies
; outside it.  Run with --profile identity-uip and --profile identity-univalence.

!(id:policy)
(: Bool (u 0))
(: true Bool)
(: false Bool)
(: p (id Bool true true))
(: q (id Bool true true))
(: not-equiv (equiv Bool Bool))

; 1. Parallel identity proofs are equal where uniqueness is the carve: the
;    uip profile declares it for every carrier (consequencesIn_hsetFragment:
;    the h-set fragment validates exactly the uniqueness laws; on data types
;    witnesses are unique, ObsId.subsingleton and obsIdStructure_sat_uip).
;    The univalence profile declares no uniqueness constant, so the term is
;    not typed there.
!(type:check (id:assumed-unique Bool true true p q) (id (id Bool true true) p q))

; 2. At the universe, uniqueness makes the identity of types thin.  A thin
;    universe does not host the groupoid laws faithfully
;    (not_hostsFaithfully_of_subset_thin), so the uip profile has no ua; the
;    univalent universe does not validate uniqueness
;    (universeIdStructure_not_uip), so the univalence profile has no such
;    constant.
!(type:check (id:assumed-unique (u 0) Bool Bool (refl Bool) (refl Bool))
             (id (id (u 0) Bool Bool) (refl Bool) (refl Bool)))

; 3. Bool is identified with itself by reflexivity under every profile, and,
;    under univalence, also through the negation equivalence.  The two
;    identifications are distinct (boolNot_ne_refl), so no sound
;    definitional equality identifies them.
!(type:of (refl Bool))
!(type:of (ua Bool Bool not-equiv))
!(type:eq (ua Bool Bool not-equiv) (refl Bool))
