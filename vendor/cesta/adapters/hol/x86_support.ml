(* Shared state normalization for exact x86 artifact proofs.
   Dependencies supply the named word and stack lemmas; every rewrite is a HOL theorem. *)

let cesta_normalize_state : tactic = fun ((asl,_) as goal) ->
  let stateful th = exists (fun v -> type_of v = `:x86state`) (frees(concl th)) in
  let facts = filter (fun th -> not(stateful th)) (map snd asl) in
  let rules = [CESTA_STACK_RESTORE; CESTA_BYTE_RESIDUE; CESTA_LOW_BYTE_JOIN;
               CESTA_SUB_ZERO; WORD_AND_0; WORD_OR_0; WORD_OR_REFL; WORD_AND_REFL;
               WORD_SUB_0; VAL_WORD_0; VAL_WORD_1; ARITH_EQ; CONJUNCT1 LT; LENGTH;
               ARITH_RULE `n <= 0 <=> n = 0`;
               ARITH_RULE `~(a < b) /\ ~(a = b) <=> b < a`;
               ARITH_RULE `~(a < b \/ a = b) <=> b < a`] in
  RULE_ASSUM_TAC (fun th -> if stateful th then
    REWRITE_RULE (rules @ facts) (CONV_RULE WORD_REDUCE_CONV (REWRITE_RULE rules th))
    else th) goal;;

(* Normalize address expressions while the preceding state's read facts are
   still available. Otherwise old-state pruning can discard a useful register
   equation merely because the instruction used p + 1*offset instead of
   p + offset. The rewrites are proved word/arithmetic equalities. *)
let cesta_normalize_step state : tactic = fun ((asl,_) as goal) ->
  let current = mk_var(state,`:x86state`) in
  let rules = [GSYM WORD_ADD_ASSOC; GSYM WORD_ADD; MULT_CLAUSES; ADD_CLAUSES;
               WORD_ADD_0] in
  let previous = map (REWRITE_RULE rules)
    (filter (fun th -> not(free_in current (concl th))) (map snd asl)) in
  RULE_ASSUM_TAC (fun th -> if free_in current (concl th) then
    REWRITE_RULE previous (REWRITE_RULE rules th) else th) goal;;

let cesta_steps exec indices =
  MAP_EVERY (fun index ->
    let state = "s" ^ string_of_int index in
    X86_VERBOSE_STEP_TAC exec state THEN cesta_normalize_step state THEN
    DISCARD_OLDSTATE_TAC state THEN CLARIFY_TAC THEN cesta_normalize_state) indices;;
