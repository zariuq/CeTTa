(* Normalize constant offsets with kernel-checked word identities, before old
   machine states are pruned. The conversion does not assume an address layout. *)

let CESTA_WORD_ULE = prove
 (`!(a:int64) b. (val a < val b \/ a = b) <=> val a <= val b`,
  REWRITE_TAC[GSYM VAL_EQ] THEN ARITH_TAC);;

let CESTA_WORD_ULE_ALT = prove
 (`!(a:int64) b. (a = b \/ val a < val b) <=> val a <= val b`,
  REWRITE_TAC[DISJ_SYM; CESTA_WORD_ULE]);;

let cesta_tisa_offset_conv tm =
  let w n = mk_comb(`word:num->int64`,mk_small_numeral n) in
  let add x n = if n=0 then x else mk_comb(mk_comb(`word_add:int64->int64->int64`,x),w n) in
  let sub x n = if n=0 then x else mk_comb(mk_comb(`word_sub:int64->int64->int64`,x),w n) in
  let isop name t = match t with Const(n,_) -> n=name | _ -> false in
  let num t = match t with Comb(Const("word",_),n) -> dest_small_numeral n | _ -> failwith "offset" in
  if type_of tm <> `:int64` then failwith "offset type";
  match tm with
  | Comb(Comb(op,Comb(Comb(inner,x),m)),n)
      when (isop "word_add" op || isop "word_sub" op) &&
           (isop "word_add" inner || isop "word_sub" inner) ->
      let m = (if isop "word_sub" inner then -1 else 1) * num m
      and n = (if isop "word_sub" op then -1 else 1) * num n in
      let rhs = if m+n<0 then sub x (-(m+n)) else add x (m+n) in
      if rhs=tm then failwith "offset unchanged" else WORD_RULE(mk_eq(tm,rhs))
  | _ -> failwith "offset shape";;

let cesta_tisa_normalize : tactic =
  RULE_ASSUM_TAC (CONV_RULE (DEPTH_CONV cesta_tisa_offset_conv)) THEN
  RULE_ASSUM_TAC (REWRITE_RULE[CESTA_WORD_ULE; CESTA_WORD_ULE_ALT;
    ARITH_RULE `a < b \/ a = b <=> a <= b`; WORD_ZX_ZX]) THEN
  cesta_normalize_state;;

let cesta_tisa_steps exec indices =
  MAP_EVERY (fun index ->
    let state="s" ^ string_of_int index in
    X86_VERBOSE_STEP_TAC exec state THEN cesta_tisa_normalize THEN
    cesta_normalize_step state THEN DISCARD_OLDSTATE_TAC state THEN
    CLARIFY_TAC THEN cesta_tisa_normalize) indices;;

(* Give containment checks one common base for a reserved stack region. Every
   conversion is proved by WORD_RULE, including modular address arithmetic. *)
let cesta_rebase_address_conv root distance tm =
  let word n=mk_comb(`word:num->int64`,mk_small_numeral n) in
  let anchor=mk_comb(mk_comb(`word_sub:int64->int64->int64`,root),word distance) in
  let number t=match t with Comb(Const("word",_),n)->dest_small_numeral n | _->failwith "offset" in
  let offset=match tm with
    | Comb(Comb(Const("word_add",_),x),n) when x=root -> number n
    | Comb(Comb(Const("word_sub",_),x),n) when x=root -> -(number n)
    | _ -> failwith "not relative to this base" in
  let delta=distance+offset in
  let rhs=if delta=0 then anchor else
    mk_comb(mk_comb((if delta<0 then `word_sub:int64->int64->int64`
                                else `word_add:int64->int64->int64`),anchor),word(abs delta)) in
  if rhs=tm then failwith "already based" else WORD_RULE(mk_eq(tm,rhs));;

let cesta_stack_frame_tac root distance =
  let normalize=DEPTH_CONV(cesta_rebase_address_conv root distance) in
  RULE_ASSUM_TAC(CONV_RULE normalize) THEN CONV_TAC normalize THEN
  MONOTONE_MAYCHANGE_TAC;;
