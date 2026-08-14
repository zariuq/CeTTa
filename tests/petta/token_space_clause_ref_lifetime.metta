!(bind! &owned_ref_space (new-space))
!(bind! &owned_order_space (new-space))

(= (assertz_space_atom_ref $space $row)
   (let*
      (($term (Predicate (cons $space $row)))
       ($_ (callPredicate (Predicate (assertz $term $ref)))))
      $ref))

(= (asserta_space_atom_ref $space $row)
   (let*
      (($term (Predicate (cons $space $row)))
       ($_ (callPredicate (Predicate (asserta $term $ref)))))
      $ref))

(= (erase_space_clause_ref $ref)
   (callPredicate (Predicate (erase $ref))))

(= (token_space_ref_roundtrip)
   (let*
      (($ref (assertz_space_atom_ref &owned_ref_space (owned-row alive)))
       ($before (collapse
          (match &owned_ref_space (owned-row $value) $value)))
       ($_ (erase_space_clause_ref $ref))
       ($after (collapse
          (match &owned_ref_space (owned-row $value) $value))))
      (token-ref-lifetime $before $after)))

(= (token_space_reused_ref)
   (let*
      (($old (assertz_space_atom_ref &owned_ref_space (owned-row old)))
       ($_old (erase_space_clause_ref $old))
       ($fresh (assertz_space_atom_ref &owned_ref_space (owned-row fresh)))
       ($_stale (collapse (erase_space_clause_ref $old)))
       ($_fresh (erase_space_clause_ref $fresh)))
      reused-token-slot-protected))

(= (token_space_asserta_order)
   (let*
      (($_tail1 (assertz_space_atom_ref &owned_order_space (ordered tail-1)))
       ($_tail2 (assertz_space_atom_ref &owned_order_space (ordered tail-2)))
       ($_front (asserta_space_atom_ref &owned_order_space (ordered front))))
      (collapse
         (match &owned_order_space (ordered $value) $value))))

; A plain PeTTa location is also native after its first add.  Temporary rows
; asserted through the Prolog-compatible clause-reference syntax must join
; that same store, remain visible to match, and erase only their occurrence.
(= (plain_named_space_ref_roundtrip)
   (let*
      (($_live (add-atom plain_owned_space (owned-row live)))
       ($ref (assertz_space_atom_ref plain_owned_space (owned-row temporary)))
       ($before (msort (collapse
          (match plain_owned_space (owned-row $value) $value))))
       ($_erase (erase_space_clause_ref $ref))
       ($after (collapse
          (match plain_owned_space (owned-row $value) $value))))
      (plain-ref-lifetime $before $after)))

; With no native owner, the same boundary form remains an ordinary Prolog
; assertion.  Its binding and clause reference must retain Prolog semantics.
(= (plain_prolog_fallthrough)
   (let*
      (($ref (let $_ (callPredicate
         (Predicate (assertz
            (Predicate (plain_prolog_only (owned-row prolog-only)))
            $clause-ref)))
         $clause-ref))
       ($before (collapse
          (let $_ (callPredicate
             (Predicate (plain_prolog_only (owned-row $value))))
             $value)))
       ($_erase (erase_space_clause_ref $ref))
       ($after (collapse
          (let $_ (callPredicate
             (Predicate (plain_prolog_only (owned-row $value))))
             $value))))
      (plain-prolog-fallthrough $before $after)))

; Native ownership is decided by successful space resolution.  A legacy
; Prolog predicate of the same spelling stays on the explicit callPredicate
; syntax; once the native space exists, boundary assertions join the native
; live row rather than silently extending the legacy predicate.
(= (plain_name_collision_precedence)
   (let*
      (($legacy-ref (let $_ (callPredicate
         (Predicate (assertz
            (Predicate (plain_collision_space (owned-row legacy)))
            $clause-ref)))
         $clause-ref))
       ($_live (add-atom
          plain_collision_space (owned-row live)))
       ($native-ref (assertz_space_atom_ref
          plain_collision_space (owned-row temporary)))
       ($native (msort (collapse
          (match plain_collision_space
             (owned-row $value) $value))))
       ($legacy (collapse
          (let $_ (callPredicate
             (Predicate (plain_collision_space
                (owned-row $value))))
             $value)))
       ($_native-erase (erase_space_clause_ref $native-ref))
       ($_legacy-erase (erase_space_clause_ref $legacy-ref)))
      (plain-collision-precedence $native $legacy)))

!(token_space_ref_roundtrip)
!(token_space_reused_ref)
!(token_space_asserta_order)
!(plain_named_space_ref_roundtrip)
!(plain_prolog_fallthrough)
!(plain_name_collision_precedence)
