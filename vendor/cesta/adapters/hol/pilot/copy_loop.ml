(* One loop proof method, instantiated with independently compiled objects.
   Offsets and instruction counts choose a proof recipe; they cannot assert
   a machine step or bypass the HOL kernel. *)

let cesta_copy_loop_rule mc exec loop_offset test_offset exit_offset =
  let statement = vsubst
    [lhs(concl mc),`copy_mc:byte list`;
     mk_small_numeral loop_offset,`loop_offset:num`;
     mk_small_numeral test_offset,`test_offset:num`;
     mk_small_numeral exit_offset,`exit_offset:num`]
   `!x z (xs:int32 list) pc stackpointer returnaddress e.
      0 < LENGTH xs /\ LENGTH xs < 2 EXP 61 /\
      nonoverlapping (word pc,LENGTH copy_mc) (z,4 * LENGTH xs) /\
      nonoverlapping (stackpointer,8) (z,4 * LENGTH xs) /\
      nonoverlapping (x,4 * LENGTH xs) (z,4 * LENGTH xs)
      ==> ensures x86
        (\s. bytes_loaded s (word pc) copy_mc /\
             read RIP s = word(pc + loop_offset) /\
             read RDI s = x /\ read RSI s = word(LENGTH xs) /\
             read RDX s = z /\ read RAX s = word 0 /\
             read RSP s = word_sub stackpointer (word 8) /\
             read (memory :> bytes64 stackpointer) s = returnaddress /\
             cesta_copy_trace e x z (LENGTH xs) stackpointer s /\
             wordlist_from_memory(x,LENGTH xs) s = xs)
        (\s. bytes_loaded s (word pc) copy_mc /\
             read RIP s = word(pc + exit_offset) /\
             read RSP s = word_sub stackpointer (word 8) /\
             read (memory :> bytes64 stackpointer) s = returnaddress /\
             cesta_copy_trace e x z (LENGTH xs) stackpointer s /\
             wordlist_from_memory(z,LENGTH xs) s = xs)
        (MAYCHANGE [RIP; RAX; RCX] ,, MAYCHANGE SOME_FLAGS ,, MAYCHANGE [events] ,,
         MAYCHANGE [memory :> bytes(z,4 * LENGTH xs)])` in
  prove(statement,
    REWRITE_TAC[SOME_FLAGS; NONOVERLAPPING_CLAUSES; fst exec] THEN
    REPEAT GEN_TAC THEN STRIP_TAC THEN
    VAL_INT64_TAC `LENGTH(xs:int32 list)` THEN
    ENSURES_WHILE_UP_TAC `LENGTH(xs:int32 list)`
      (mk_comb(`(+)(pc:num)`,mk_small_numeral loop_offset))
      (mk_comb(`(+)(pc:num)`,mk_small_numeral test_offset))
      `\i s. read RDI s = x /\ read RSI s = word(LENGTH xs) /\
             read RDX s = z /\ read RAX s = word i /\
             read RSP s = word_sub stackpointer (word 8) /\
             read (memory :> bytes64 stackpointer) s = returnaddress /\
             cesta_copy_trace e x z (LENGTH xs) stackpointer s /\
             wordlist_from_memory(x,LENGTH xs) s = xs /\
             wordlist_from_memory(z,i) s = cesta_prefix i xs` THEN
    ASM_SIMP_TAC[ARITH_RULE `0 < n ==> ~(n = 0)`] THEN REPEAT CONJ_TAC THENL
     [REWRITE_TAC[CESTA_PREFIX_ZERO; WORDLIST_FROM_MEMORY_CLAUSES; CESTA_WORDS32] THEN
      X86_SIM_TAC exec [];

      X_GEN_TAC `i:num` THEN STRIP_TAC THEN VAL_INT64_TAC `i:num` THEN
      REWRITE_TAC[CESTA_WORDS32; cesta_copy_trace] THEN
      ENSURES_INIT_TAC "s0" THEN TRY STRIP_EXISTS_ASSUM_TAC THEN
      SUBGOAL_THEN `i + 1 < 2 EXP 64` ASSUME_TAC THENL [ASM_ARITH_TAC; ALL_TAC] THEN
      SUBGOAL_THEN
       `read (memory :> bytes32 (word_add x (word(4 * i)))) (s0:x86state) = EL i (xs:int32 list)`
      ASSUME_TAC THENL [ASM_MESON_TAC[CESTA_READ32; CESTA_WORDS32]; ALL_TAC] THEN
      SUBGOAL_THEN `nonoverlapping (z:int64,4 * i) (word_add z (word(4 * i)),4)`
      ASSUME_TAC THENL [NONOVERLAPPING_TAC; ALL_TAC] THEN
      X86_STEPS_TAC exec (1--3) THEN ENSURES_FINAL_STATE_TAC THEN
      ASM_REWRITE_TAC[GSYM WORD_ADD] THEN
      VAL_INT64_TAC `i + 1` THEN
      SUBGOAL_THEN
       `wordlist_of_num (i + 1) (read (memory :> bytes(z,4 * (i + 1))) s3) =
        cesta_prefix (i + 1) (xs:int32 list)`
      ASSUME_TAC THENL
       [MP_TAC(REWRITE_RULE[CESTA_WORDS32; ADD1]
         (ISPECL [`z:int64`; `i:num`; `xs:int32 list`; `s3:x86state`] CESTA_PREFIX_MEMORY32)) THEN
        ASM_SIMP_TAC[WORD_ZX_ZX; DIMINDEX_32; DIMINDEX_64; ARITH_LE; ARITH_LT];
        ASM_REWRITE_TAC[] THEN DISCHARGE_MEMSAFE_ASM_TAC];

      X_GEN_TAC `i:num` THEN STRIP_TAC THEN VAL_INT64_TAC `i:num` THEN
      REWRITE_TAC[CESTA_WORDS32; cesta_copy_trace] THEN
      X86_SIM_TAC ~preprocess_tac:(TRY STRIP_EXISTS_ASSUM_TAC) exec (1--2) THEN
      DISCHARGE_MEMSAFE_ASM_TAC;

      REWRITE_TAC[CESTA_PREFIX_ALL; CESTA_WORDS32; cesta_copy_trace] THEN
      X86_SIM_TAC ~preprocess_tac:(TRY STRIP_EXISTS_ASSUM_TAC) exec (1--2) THEN
      DISCHARGE_MEMSAFE_ASM_TAC]);;

let COPY_GCC_LOOP = cesta_copy_loop_rule copy_gcc_body_mc COPY_GCC_EXEC 0x32 0x3c 0x41;;
let COPY_CLANG_LOOP = cesta_copy_loop_rule copy_clang_body_mc COPY_CLANG_EXEC 0x70 0x79 0x7e;;
