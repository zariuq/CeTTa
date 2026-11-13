(* Compose argument checks, the shared counted loop and the real ABI return.
   The native failure hook is outside the admitted input contract. *)

let cesta_sim_return exec steps =
  GHOST_INTRO_TAC `rax_before_return:int64` `read RAX` THEN
  GHOST_INTRO_TAC `r8_before_return:int64` `read R8` THEN
  ENSURES_INIT_TAC "s0" THEN TRY STRIP_EXISTS_ASSUM_TAC THEN
  cesta_steps exec (1--steps) THEN ENSURES_FINAL_STATE_TAC THEN
  ASM_REWRITE_TAC[CESTA_STACK_RESTORE] THEN
  SUBGOAL_THEN
   `(EventLoad(stackpointer:int64,8):uarch_event) =
    EventLoad(word_add (word_sub stackpointer (word 8)) (word 8),8)`
  ASSUME_TAC THENL [REWRITE_TAC[CESTA_STACK_RESTORE]; ALL_TAC] THEN
  ASM_REWRITE_TAC[] THEN DISCHARGE_SAFETY_PROPERTY_TAC;;

let cesta_copy_function_rule mc exec loop_thm loop_offset exit_offset
      positive_steps zero_steps zero_capacity_steps =
  let at offset = mk_comb(`(+)(pc:num)`,mk_small_numeral offset) in
  let statement = vsubst [lhs(concl mc),`copy_mc:byte list`]
   `!x z (xs:int32 list) m pc stackpointer returnaddress e.
      LENGTH xs <= m /\ m < 2 EXP 61 /\
      (LENGTH xs = 0 \/ ~(x = word 0)) /\ (m = 0 \/ ~(z = word 0)) /\
      word_and x (word 3) = word 0 /\ word_and z (word 3) = word 0 /\
      val x + 4 * LENGTH xs <= 2 EXP 64 /\ val z + 4 * m <= 2 EXP 64 /\
      8 <= val stackpointer /\
      ALL (nonoverlapping (word_sub stackpointer (word 8),16))
        [(word pc,LENGTH copy_mc); (x,4 * LENGTH xs); (z,4 * m)] /\
      nonoverlapping (word pc,LENGTH copy_mc) (z,4 * m) /\
      nonoverlapping (x,4 * LENGTH xs) (z,4 * m)
      ==> ensures x86
        (\s. bytes_loaded s (word pc) copy_mc /\ read RIP s = word pc /\
             read RSP s = stackpointer /\
             read (memory :> bytes64 stackpointer) s = returnaddress /\
             C_ARGUMENTS [x; word(LENGTH xs); z; word m] s /\
             read events s = e /\
             wordlist_from_memory(x,LENGTH xs) s = xs)
        (\s. read RIP s = returnaddress /\
             read RSP s = word_add stackpointer (word 8) /\
             cesta_copy_trace e x z (LENGTH xs) stackpointer s /\
             wordlist_from_memory(z,LENGTH xs) s = xs)
        (MAYCHANGE [RSP] ,, MAYCHANGE_REGS_AND_FLAGS_PERMITTED_BY_ABI ,,
         MAYCHANGE [memory :> bytes(z,4 * LENGTH xs);
                    memory :> bytes(word_sub stackpointer (word 8),8)])` in
  prove(statement,
    REWRITE_TAC[C_ARGUMENTS; SOME_FLAGS; ALL; NONOVERLAPPING_CLAUSES;
                MAYCHANGE_REGS_AND_FLAGS_PERMITTED_BY_ABI; fst exec] THEN
    REPEAT GEN_TAC THEN DISCH_THEN(REPEAT_TCL CONJUNCTS_THEN ASSUME_TAC) THEN
    SUBGOAL_THEN `LENGTH(xs:int32 list) < 2 EXP 61 /\ ~(m < LENGTH(xs:int32 list)) /\
                 ~(2305843009213693951 < m)`
    STRIP_ASSUME_TAC THENL [ASM_ARITH_TAC; ALL_TAC] THEN
    VAL_INT64_TAC `m:num` THEN VAL_INT64_TAC `LENGTH(xs:int32 list)` THEN
    SUBGOAL_THEN
     `word_ushr (word m:int64) 61 = word 0 /\
      word_ushr (word(LENGTH(xs:int32 list)):int64) 61 = word 0 /\
      word_and (word_zx (x:int64):byte) (word 3) = word 0 /\
      word_and (word_zx (z:int64):byte) (word 3) = word 0 /\
      word_and (word_zx (z:int64):int32) (word 3) = word 0`
    STRIP_ASSUME_TAC THENL [ASM_SIMP_TAC[CESTA_LENGTH_SHIFT; CESTA_ALIGNED_BYTE; CESTA_ALIGNED_WORD32]; ALL_TAC] THEN
    SUBGOAL_THEN
     `val(word_zx (x:int64):byte) MOD 4 = 0 /\
      val(word_zx (z:int64):byte) MOD 4 = 0 /\
      val(word_zx (z:int64):int32) MOD 4 = 0`
    STRIP_ASSUME_TAC THENL [ASM_MESON_TAC[CESTA_MASK_MOD4; VAL_WORD_0]; ALL_TAC] THEN
    ASM_CASES_TAC `LENGTH(xs:int32 list) = 0` THENL
     [SUBGOAL_THEN `xs:int32 list = []` SUBST_ALL_TAC THENL
       [ASM_MESON_TAC[LENGTH_EQ_NIL]; ALL_TAC] THEN
      REWRITE_TAC[WORDLIST_FROM_MEMORY_CLAUSES; LENGTH; cesta_copy_trace] THEN
      ASM_CASES_TAC `m = 0` THENL
       [ASM_REWRITE_TAC[] THEN cesta_sim_return exec zero_capacity_steps;
        SUBGOAL_THEN `~(val(z:int64) = 0)` ASSUME_TAC THENL [ASM_MESON_TAC[VAL_EQ_0]; ALL_TAC] THEN
        ASM_REWRITE_TAC[] THEN cesta_sim_return exec zero_steps];
      ALL_TAC] THEN
    SUBGOAL_THEN `0 < LENGTH(xs:int32 list) /\ ~(m = 0)`
    STRIP_ASSUME_TAC THENL [ASM_ARITH_TAC; ALL_TAC] THEN
    SUBGOAL_THEN `~(x:int64 = word 0) /\ ~(z:int64 = word 0)` STRIP_ASSUME_TAC THENL
     [ASM_MESON_TAC[]; ALL_TAC] THEN
    SUBGOAL_THEN `~(val(x:int64) = 0) /\ ~(val(z:int64) = 0)` STRIP_ASSUME_TAC THENL
     [ASM_MESON_TAC[VAL_EQ_0]; ALL_TAC] THEN
    ENSURES_SEQUENCE_TAC (at loop_offset)
     `\s. read RDI s = x /\ read RSI s = word(LENGTH xs) /\
          read RDX s = z /\ read RAX s = word 0 /\
          read RSP s = word_sub stackpointer (word 8) /\
          read (memory :> bytes64 stackpointer) s = returnaddress /\
          cesta_copy_trace e x z (LENGTH xs) stackpointer s /\
          wordlist_from_memory(x,LENGTH xs) s = (xs:int32 list)` THEN
    CONJ_TAC THENL
     [REWRITE_TAC[CESTA_WORDS32; cesta_copy_trace] THEN
      GHOST_INTRO_TAC `rax_before_entry:int64` `read RAX` THEN
      GHOST_INTRO_TAC `r8_before_entry:int64` `read R8` THEN ENSURES_INIT_TAC "s0" THEN
      cesta_steps exec (1--positive_steps) THEN ENSURES_FINAL_STATE_TAC THEN ASM_REWRITE_TAC[] THEN
      DISCHARGE_SAFETY_PROPERTY_TAC;
      ALL_TAC] THEN
    ENSURES_SEQUENCE_TAC (at exit_offset)
     `\s. read RSP s = word_sub stackpointer (word 8) /\
          read (memory :> bytes64 stackpointer) s = returnaddress /\
          cesta_copy_trace e x z (LENGTH xs) stackpointer s /\
          wordlist_from_memory(z,LENGTH xs) s = (xs:int32 list)` THEN
    CONJ_TAC THENL
     [MATCH_MP_TAC ENSURES_FRAME_SUBSUMED THEN
      EXISTS_TAC `(MAYCHANGE [RIP; RAX; RCX] ,, MAYCHANGE SOME_FLAGS ,, MAYCHANGE [events] ,,
                   MAYCHANGE [memory :> bytes(z,4 * LENGTH(xs:int32 list))])` THEN
      REWRITE_TAC[SOME_FLAGS] THEN
      CONJ_TAC THENL [SUBSUMED_MAYCHANGE_TAC; ALL_TAC] THEN
      MATCH_MP_TAC (REWRITE_RULE[SOME_FLAGS; NONOVERLAPPING_CLAUSES; fst exec] loop_thm) THEN
      ASM_REWRITE_TAC[] THEN REPEAT CONJ_TAC THEN NONOVERLAPPING_TAC;
      REWRITE_TAC[CESTA_WORDS32; cesta_copy_trace] THEN cesta_sim_return exec 2]);;

let COPY_GCC_CORRECT = cesta_copy_function_rule copy_gcc_body_mc COPY_GCC_EXEC
  COPY_GCC_LOOP 0x32 0x41 25 20 8;;
let COPY_CLANG_CORRECT = cesta_copy_function_rule copy_clang_body_mc COPY_CLANG_EXEC
  COPY_CLANG_LOOP 0x70 0x7e 37 37 37;;
