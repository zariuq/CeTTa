(* Old-state pruning must distinguish the processor state from saved memory
   values. A raw-memory read's argument is not an x86state. Dropping that fact
   loses a stable table observation without helping symbolic-state cleanup. *)

let cesta_discard_oldstate_tac name : tactic =
  let current=mk_var(name,`:x86state`) in
  let rec states bound tm = match tm with
    | Comb(Comb(Const("read",_),_),state) when type_of state = `:x86state` ->
        if mem state bound then [] else [state]
    | Comb(f,x) -> union (states bound f) (states bound x)
    | Abs(v,body) -> states (v::bound) body
    | _ -> [] in
  DISCARD_ASSUMPTIONS_TAC (fun theorem ->
    let dependencies=states [] (concl theorem) in
    dependencies <> [] && dependencies <> [current]);;

let CESTA_MEMORY_COMPOSED_WRITE = prove
 (`!c:((int64->byte),A)component. !value s.
    read memory (write (memory :> c) value s) =
    write c value (read memory s)`,
  REPEAT GEN_TAC THEN REWRITE_TAC[WRITE_COMPONENT_COMPOSE] THEN
  CONV_TAC(DEPTH_CONV COMPONENT_READ_OVER_WRITE_CONV) THEN REFL_TAC);;

(* Retain the parent memory value when a child component is written. The
   equality is derived from the processor's actual instruction update. *)
let cesta_memory_step_tac exec state : tactic =
  X86_STEP_TAC exec [] state None (fun update ->
    let memory_update = AP_TERM `read memory:x86state->int64->byte` update in
    let memory_update = REWRITE_RULE[CESTA_MEMORY_COMPOSED_WRITE] memory_update in
    let memory_update = CONV_RULE
      (LAND_CONV(DEPTH_CONV COMPONENT_READ_OVER_WRITE_CONV)) memory_update in
    STRIP_TAC THEN ASSUME_TAC(SYM memory_update));;

let (cesta_bigstep_tac:(thm * thm option array)->string->tactic) =
  GEN_BIGSTEP_TAC "X86" bytes_loaded_update cesta_discard_oldstate_tac;;
