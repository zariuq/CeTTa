(= (make_owned_clause_ref $clause)
   (let*
      (($_ (callPredicate (Predicate (assertz $clause $ref)))))
      $ref))

(= (erase_owned_clause_ref $ref)
   (callPredicate (Predicate (erase $ref))))

(= (owned_clause_ref_roundtrip)
   (let*
      (($ref (make_owned_clause_ref (owned_clause alive)))
       ($_ (erase_owned_clause_ref $ref)))
      clause-ref-released))

(= (stale_clause_ref_result)
   (let*
      (($ref (make_owned_clause_ref (owned_clause stale)))
       ($_ (erase_owned_clause_ref $ref)))
      (collapse (erase_owned_clause_ref $ref))))

(= (reused_clause_ref_result)
   (let*
      (($old (make_owned_clause_ref (owned_clause old)))
       ($_old (erase_owned_clause_ref $old))
       ($fresh (make_owned_clause_ref (owned_clause fresh)))
       ($_stale (collapse (erase_owned_clause_ref $old)))
       ($_fresh (erase_owned_clause_ref $fresh)))
      reused-slot-protected))

(= (foreign_runtime_clause_ref
      (PettaClauseRef $runtime $slot $generation))
   (PettaClauseRef (+ $runtime 1000000) $slot $generation))

(= (foreign_runtime_clause_ref_result)
   (let*
      (($ref (make_owned_clause_ref (owned_clause cross-runtime)))
       ($foreign (foreign_runtime_clause_ref $ref))
       ($_foreign (collapse (erase_owned_clause_ref $foreign)))
       ($_real (erase_owned_clause_ref $ref)))
      foreign-runtime-protected))

!(owned_clause_ref_roundtrip)
!(stale_clause_ref_result)
!(reused_clause_ref_result)
!(foreign_runtime_clause_ref_result)
