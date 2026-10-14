; The identity profile is space data.  A space selects one with
; (id:profile NAME), read like (set:profile NAME); a space that selects none
; has the run's --profile.  One program holds spaces in different modes, and
; each judgment is read in the mode of the space it is posed in.  Run under
; every --profile: only &plain and &self follow it.

!(bind! &uip (new-space))
!(add-atom &uip (id:profile identity-uip))
!(bind! &regions (new-space))
!(add-atom &regions (id:profile identity-scoped))
!(bind! &plain (new-space))
!(bind! &both (new-space))
!(add-atom &both (id:profile identity-uip))
!(add-atom &both (id:profile identity-j))

!(add-atom &uip (: Nat (u 0)))
!(add-atom &uip (: z Nat))
!(add-atom &uip (: loop (id Nat z z)))
!(add-atom &regions (: Nat (u 0)))
!(add-atom &regions (: z Nat))
!(add-atom &regions (: loop (id Nat z z)))
!(add-atom &plain (: Nat (u 0)))
!(add-atom &plain (: z Nat))
!(add-atom &plain (: loop (id Nat z z)))
!(add-atom &both (: Nat (u 0)))
!(add-atom &both (: z Nat))
!(add-atom &both (: loop (id Nat z z)))

; 1. Each space reports its own mode; a conflicting selection reports none.
!(id:policy &uip)
!(id:policy &regions)
!(id:policy &plain)
!(id:policy)
!(id:policy &both)

; 2. Uniqueness everywhere exists only where the space selects it.
!(type:check &uip (id:assumed-unique Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))
!(type:check &regions (id:assumed-unique Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))
!(type:check &plain (id:assumed-unique Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))
!(type:check &both (id:assumed-unique Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))

; 3. A region is admitted only where the mode admits regions, and its
;    assumption is published into that space alone.
!(id:region &regions Nat)
!(type:check &regions (id:assumed-unique@Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))
!(id:region &uip Nat)
!(id:region &both Nat)
!(type:check &uip (id:assumed-unique@Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))

; 4. Elimination is the same in every mode, a conflicting one included, and
;    neither assumption changes definitional equality.
!(type:eq &uip (id:eliminate Nat z (lam y (lam e Nat)) z z (refl z)) z)
!(type:eq &regions (id:eliminate Nat z (lam y (lam e Nat)) z z (refl z)) z)
!(type:eq &both (id:eliminate Nat z (lam y (lam e Nat)) z z (refl z)) z)
!(type:eq &uip (refl z) loop)
