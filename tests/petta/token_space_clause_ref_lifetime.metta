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

!(token_space_ref_roundtrip)
!(token_space_reused_ref)
!(token_space_asserta_order)
