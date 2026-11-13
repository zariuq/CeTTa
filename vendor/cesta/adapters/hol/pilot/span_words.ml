(* Spatial and word-list facts used by compiler-output span proofs.
   s2n-bignum is an external dependency; these proofs instantiate its public
   memory model without introducing a new machine semantics. *)

needs "x86/proofs/base.ml";;
needs "common/wordlist.ml";;
needs "x86/proofs/consttime.ml";;

let CESTA_CONTAINED_REFL = prove
 (`!(a:int64) n. contained (a,n) (a,n)`,
  REWRITE_TAC[contained; CONTAINED_MODULO_REFL; LE_REFL]);;

let CESTA_CONTAINED_PREFIX = prove
 (`!(a:int64) n m. n <= m ==> contained (a,n) (a,m)`,
  REPEAT STRIP_TAC THEN
  ASM_REWRITE_TAC[contained; CONTAINED_MODULO_REFL]);;

let CESTA_CONTAINED_OFFSET = prove
 (`!(a:int64) k n m.
    k + n <= m ==> contained (word_add a (word k),n) (a,m)`,
  REPEAT STRIP_TAC THEN REWRITE_TAC[contained] THEN CONTAINED_TAC);;

let CESTA_APPEND_EVENT_SPLIT = prove
 (`!(e1:uarch_event list) e2 rr wr.
    memaccess_inbounds e1 rr wr /\ memaccess_inbounds e2 rr wr
    ==> memaccess_inbounds (APPEND e1 e2) rr wr`,
  REWRITE_TAC[MEMACCESS_INBOUNDS_APPEND]);;

(* The pre-existing event suffix is unrestricted. Only accesses made by the
   current call must lie in its declared source/destination/stack regions. *)
let cesta_copy_trace = new_definition
 `cesta_copy_trace e (x:int64) z n stackpointer s <=>
    ?e2. read events s = APPEND e2 e /\
         memaccess_inbounds e2
           [(x,4 * n); (word_sub stackpointer (word 8),16)]
           [(z,4 * n); (word_sub stackpointer (word 8),8)]`;;

let CESTA_WORDS32 = prove
 (`!a n s. wordlist_from_memory(a,n) s : int32 list =
           wordlist_of_num n (read (memory :> bytes(a,4 * n)) s)`,
  REPEAT GEN_TAC THEN
  MP_TAC(ISPECL [`a:int64`; `n:num`; `s:x86state`]
    (INST_TYPE [`:4`,`:N`] WORDLIST_FROM_MEMORY)) THEN
  REWRITE_TAC[DIMINDEX_4]);;

let CESTA_READ32 = prove
 (`!(xs:int32 list) (a:int64) (s:x86state) i.
      i < LENGTH xs /\ wordlist_from_memory(a,LENGTH xs) s = xs
      ==> read (memory :> bytes32 (word_add a (word (4 * i)))) s = EL i xs`,
  REPEAT STRIP_TAC THEN
  MP_TAC(ISPECL [`a:int64`; `LENGTH(xs:int32 list)`; `i:num`; `s:x86state`]
    (INST_TYPE [`:4`,`:N`] EL_WORDLIST_FROM_MEMORY)) THEN
  ASM_REWRITE_TAC[BYTES32_WBYTES; DIMINDEX_4; EQ_SYM_EQ]);;

let cesta_prefix = new_definition
 `cesta_prefix n (xs:int32 list) = list_of_seq (\i. EL i xs) n`;;

let CESTA_PREFIX_ZERO = prove
 (`!xs:int32 list. cesta_prefix 0 xs = []`,
  REWRITE_TAC[cesta_prefix; list_of_seq]);;

let CESTA_PREFIX_ALL = prove
 (`!xs:int32 list. cesta_prefix (LENGTH xs) xs = xs`,
  REWRITE_TAC[cesta_prefix; LIST_OF_SEQ_EQ_SELF]);;

let CESTA_PREFIX_SUC = prove
 (`!i (xs:int32 list). cesta_prefix (SUC i) xs = APPEND (cesta_prefix i xs) [EL i xs]`,
  REWRITE_TAC[cesta_prefix; list_of_seq]);;

let CESTA_PREFIX_MEMORY32 = prove
 (`!(a:int64) i (xs:int32 list) (s:x86state).
      wordlist_from_memory(a,i) s = cesta_prefix i xs /\
      read (memory :> bytes32 (word_add a (word (4 * i)))) s = EL i xs
      ==> wordlist_from_memory(a,SUC i) s = cesta_prefix (SUC i) xs`,
  REPEAT STRIP_TAC THEN
  MP_TAC(ISPECL [`a:int64`; `i:num`; `s:x86state`]
    (INST_TYPE [`:4`,`:N`] (CONJUNCT2 WORDLIST_FROM_MEMORY_CLAUSES))) THEN
  ASM_REWRITE_TAC[DIMINDEX_4; GSYM BYTES32_WBYTES; CESTA_PREFIX_SUC]);;

let CESTA_LENGTH_SHIFT = prove
 (`!n. n < 2 EXP 61 ==> word_ushr (word n:int64) 61 = word 0`,
  REPEAT STRIP_TAC THEN REWRITE_TAC[GSYM VAL_EQ; VAL_WORD_USHR; VAL_WORD_0] THEN
  ASM_SIMP_TAC[VAL_WORD_EQ; DIMINDEX_64; ARITH_RULE `n < 2 EXP 61 ==> n < 2 EXP 64`;
               DIV_LT]);;

let CESTA_ALIGNED_BYTE = prove
 (`!x:int64. word_and x (word 3) = word 0
      ==> word_and (word_zx x:byte) (word 3) = word 0`,
  CONV_TAC WORD_BLAST);;

let CESTA_ALIGNED_WORD32 = prove
 (`!x:int64. word_and x (word 3) = word 0
      ==> word_and (word_zx x:int32) (word 3) = word 0`,
  CONV_TAC WORD_BLAST);;

(* Preserve a nonnegative control fact when zero-length equalities would be
   substituted away by symbolic-state clarification. *)
let span_zero_cases (length_tm:term) : tactic =
  let zero_tm = mk_eq(length_tm,`0:num`) in
  let numeric_equality tm th =
    let c = concl th in
    is_eq c && type_of(rhs c) = `:num` && c = tm in
  fun ((asl,_) as goal) ->
    (ASM_CASES_TAC zero_tm THENL
     [(fun ((asl,_) as goal) ->
        let h = snd(List.find (fun (_,th) -> numeric_equality zero_tm th) asl) in
        ASSUME_TAC(MP (SPEC length_tm
          (ARITH_RULE `!n:num. n = 0 ==> ~(0 < n)`)) h) goal);
      (fun ((asl,_) as goal) ->
        let h = snd(List.find (fun (_,th) -> concl th = mk_neg zero_tm) asl) in
        ASSUME_TAC(MP (SPEC length_tm
          (ARITH_RULE `!n:num. ~(n = 0) ==> 0 < n`)) h) goal)]) goal;;

(* Turn known control propositions into explicit Boolean equalities.  Unlike
   general simplification, this resolves a decoded conditional directly from
   either a positive assumption or its negation. *)
let simplify_conditionals_with_facts facts th =
  let rules = List.filter_map (fun h ->
    try Some(if is_neg(concl h) then EQF_INTRO h else EQT_INTRO h)
    with Failure _ -> None) facts in
  REWRITE_RULE (COND_CLAUSES::NOT_CLAUSES::rules) th;;

let CESTA_STACK_RESTORE = prove
 (`!sp:int64. word_add (word_sub sp (word 8)) (word 8) = sp /\
              word_add (word_sub sp (word 8)) (word 16) = word_add sp (word 8)`,
  CONV_TAC WORD_RULE);;

let CESTA_MASK_MOD4 = prove
 (`!x:N word. val(word_and x (word 3)) = val x MOD 4`,
  GEN_TAC THEN MP_TAC(ISPECL [`x:N word`; `2`] VAL_WORD_AND_MASK_WORD) THEN
  CONV_TAC NUM_REDUCE_CONV THEN REWRITE_TAC[]);;

let CESTA_BYTE_RESIDUE = prove
 (`!x:int64. (word(val x MOD 256):byte) = word_zx x`,
  GEN_TAC THEN REWRITE_TAC[word_zx] THEN
  GEN_REWRITE_TAC RAND_CONV [GSYM WORD_MOD_SIZE] THEN
  REWRITE_TAC[DIMINDEX_8] THEN CONV_TAC NUM_REDUCE_CONV);;

let CESTA_LOW_BYTE_JOIN = prove
 (`!(h:56 word) (b:byte). (word_zx (word_join h b:int64):byte) = b`,
  CONV_TAC WORD_BLAST);;

let CESTA_VAL_READ_BYTES64 = prove
 (`!a s. val(read(memory :> bytes64 a)s) = read(bytes(a,8)) (read memory s)`,
  REPEAT GEN_TAC THEN
  REWRITE_TAC[bytes64; asword; through; READ_COMPONENT_COMPOSE; read;
              VAL_WORD; DIMINDEX_64] THEN
  MATCH_MP_TAC MOD_LT THEN
  SUBST1_TAC(ARITH_RULE `64 = 8 * 8`) THEN REWRITE_TAC[READ_BYTES_BOUND]);;

let CESTA_READ_BYTES8_64 = prove
 (`!a s. read(memory :> bytes8 a)s =
         word_zx(read(memory :> bytes64 a)s)`,
  REPEAT GEN_TAC THEN
  REWRITE_TAC[GSYM VAL_EQ; VAL_WORD_ZX_GEN; CESTA_VAL_READ_BYTES64] THEN
  REWRITE_TAC[bytes8; asword; through; READ_COMPONENT_COMPOSE; read;
              VAL_WORD; DIMINDEX_8] THEN
  SIMP_TAC[READ_BYTES_BOUND; MOD_LT] THEN REWRITE_TAC[DIMINDEX_8] THEN
  CONV_TAC NUM_REDUCE_CONV THEN
  MP_TAC(ISPECL [`a:int64`; `8`; `1`; `read memory s`] READ_BYTES_MOD) THEN
  CONV_TAC NUM_REDUCE_CONV THEN DISCH_TAC THEN ASM_REWRITE_TAC[] THEN
  MATCH_MP_TAC MOD_LT THEN
  SUBST1_TAC(ARITH_RULE `256 = 2 EXP (8 * 1)`) THEN
  REWRITE_TAC[READ_BYTES_BOUND]);;

let CESTA_READ_BYTES32_64 = prove
 (`!a s. read(memory :> bytes32 a)s =
         word_zx(read(memory :> bytes64 a)s)`,
  REPEAT GEN_TAC THEN
  REWRITE_TAC[GSYM VAL_EQ; VAL_WORD_ZX_GEN; CESTA_VAL_READ_BYTES64] THEN
  REWRITE_TAC[bytes32; asword; through; READ_COMPONENT_COMPOSE; read;
              VAL_WORD; DIMINDEX_32] THEN
  SIMP_TAC[READ_BYTES_BOUND; MOD_LT] THEN REWRITE_TAC[DIMINDEX_32] THEN
  CONV_TAC NUM_REDUCE_CONV THEN
  MP_TAC(ISPECL [`a:int64`; `8`; `4`; `read memory s`] READ_BYTES_MOD) THEN
  CONV_TAC NUM_REDUCE_CONV THEN DISCH_TAC THEN ASM_REWRITE_TAC[] THEN
  MATCH_MP_TAC MOD_LT THEN
  SUBST1_TAC(ARITH_RULE `4294967296 = 2 EXP (8 * 4)`) THEN
  REWRITE_TAC[READ_BYTES_BOUND]);;

let CESTA_SUB_ZERO = prove
 (`!(a:N word) b. val(word_sub a b) = 0 <=> val a = val b`,
  REWRITE_TAC[VAL_WORD_SUB_EQ_0; VAL_EQ]);;
