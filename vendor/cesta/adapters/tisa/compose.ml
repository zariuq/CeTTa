(* Compose the real calls in each compiler's wrapper. The core owns the local
   stack area; the standard stack rule restores the surrounding saved registers. *)

let mixed_bindings image frame_reg source_reg destination_reg capacity_reg
      entry exit stackoff locals =
  [lhs(concl image),`mixed_image:byte list`;
   frame_reg,mk_var("frame_register",type_of `RAX`);
   source_reg,mk_var("source_register",type_of `RAX`);
   destination_reg,mk_var("destination_register",type_of `RAX`);
   capacity_reg,mk_var("capacity_register",type_of `RAX`);
   mk_small_numeral entry,`core_entry:num`;
   mk_small_numeral exit,`core_exit:num`;
   mk_small_numeral stackoff,`outer_frame_bytes:num`;
   mk_small_numeral(48+locals),`scratch_bytes:num`];;

let mixed_core_statement bindings = vsubst bindings
 `!frame (x:int64) (xs:int32 list) z m pc stackpointer.
     LENGTH xs < 2 EXP 61 /\ m < 2 EXP 61 /\
     (LENGTH xs = 0 \/ ~(x = word 0)) /\ (m = 0 \/ ~(z = word 0)) /\
     word_and x (word 3) = word 0 /\ word_and z (word 3) = word 0 /\
     val x + 4 * LENGTH xs <= 2 EXP 64 /\ val z + 4 * m <= 2 EXP 64 /\
     val frame + 192 <= 2 EXP 64 /\
     128 <= val(word_add stackpointer (word outer_frame_bytes)) /\
     ALL (nonoverlapping
       (word_sub (word_add stackpointer (word outer_frame_bytes)) (word 128),136))
       [(word pc,LENGTH mixed_image); (frame,192); (x,4 * LENGTH xs); (z,4 * m)] /\
     ALL (nonoverlapping (frame,192)) [(x,4 * LENGTH xs); (z,4 * m)] /\
     nonoverlapping (word pc,LENGTH mixed_image) (frame,192) /\
     nonoverlapping (word pc,LENGTH mixed_image) (z,4 * m) /\
     nonoverlapping (x,4 * LENGTH xs) (z,4 * m)
     ==> ensures x86
       (\s. bytes_loaded s (word pc) mixed_image /\ read RIP s = word(pc + core_entry) /\
            read RSP s = stackpointer /\ C_ARGUMENTS [frame;word(LENGTH xs);word m] s /\
            read frame_register s = frame /\ read source_register s = x /\
            read destination_register s = z /\ read capacity_register s = word m /\
            wordlist_from_memory(x,LENGTH xs) s = xs /\
            read (memory :> bytes64(word_add frame (word 160))) s = word 0)
       (\s. read RIP s = word(pc + core_exit) /\ read RSP s = stackpointer /\
            read RAX s = word(MIN (LENGTH xs) m) /\
            wordlist_from_memory(z,MIN (LENGTH xs) m) s = cesta_prefix (MIN (LENGTH xs) m) xs /\
            wordlist_from_memory(x,LENGTH xs) s = xs /\
            read (memory :> bytes64(word_add frame (word 144))) s = word 1 /\
            read (memory :> bytes64(word_add frame (word 152))) s = word(MIN (LENGTH xs) m) /\
            read (memory :> bytes64(word_add frame (word 160))) s = word 0 /\
            read (memory :> bytes64(word_add frame (word 168))) s = word 2)
       (MAYCHANGE [RSP] ,, MAYCHANGE_REGS_AND_FLAGS_PERMITTED_BY_ABI ,,
        MAYCHANGE [frame_register;source_register;destination_register;capacity_register] ,,
        MAYCHANGE [memory :> bytes(word_sub stackpointer (word 48),scratch_bytes);
                   memory :> bytes(word_add frame (word 144),32);
                   memory :> bytes(z,4 * MIN (LENGTH xs) m)])`;;

let mixed_full_statement image entry =
  vsubst [lhs(concl image),`mixed_image:byte list`;mk_small_numeral entry,`entry:num`]
 `!frame (x:int64) (xs:int32 list) z m pc stackpointer returnaddress.
     LENGTH xs < 2 EXP 61 /\ m < 2 EXP 61 /\
     (LENGTH xs = 0 \/ ~(x = word 0)) /\ (m = 0 \/ ~(z = word 0)) /\
     word_and x (word 3) = word 0 /\ word_and z (word 3) = word 0 /\
     val x + 4 * LENGTH xs <= 2 EXP 64 /\ val z + 4 * m <= 2 EXP 64 /\
     val frame + 192 <= 2 EXP 64 /\ 128 <= val stackpointer /\
     ALL (nonoverlapping (word_sub stackpointer (word 128),136))
       [(word pc,LENGTH mixed_image); (frame,192); (x,4 * LENGTH xs); (z,4 * m)] /\
     ALL (nonoverlapping (frame,192)) [(x,4 * LENGTH xs); (z,4 * m)] /\
     nonoverlapping (word pc,LENGTH mixed_image) (frame,192) /\
     nonoverlapping (word pc,LENGTH mixed_image) (z,4 * m) /\
     nonoverlapping (x,4 * LENGTH xs) (z,4 * m)
     ==> ensures x86
       (\s. bytes_loaded s (word pc) mixed_image /\ read RIP s = word(pc + entry) /\
            read RSP s = stackpointer /\ read (memory :> bytes64 stackpointer) s = returnaddress /\
            C_ARGUMENTS [frame;x;word(LENGTH xs);z;word m] s /\
            wordlist_from_memory(x,LENGTH xs) s = xs /\
            read (memory :> bytes64(word_add frame (word 160))) s = word 0)
       (\s. read RIP s = returnaddress /\ read RSP s = word_add stackpointer (word 8) /\
            read RAX s = word(MIN (LENGTH xs) m) /\
            wordlist_from_memory(z,MIN (LENGTH xs) m) s = cesta_prefix (MIN (LENGTH xs) m) xs /\
            wordlist_from_memory(x,LENGTH xs) s = xs /\
            read (memory :> bytes64(word_add frame (word 144))) s = word 1 /\
            read (memory :> bytes64(word_add frame (word 152))) s = word(MIN (LENGTH xs) m) /\
            read (memory :> bytes64(word_add frame (word 160))) s = word 0 /\
            read (memory :> bytes64(word_add frame (word 168))) s = word 2)
       (MAYCHANGE [RSP] ,, MAYCHANGE_REGS_AND_FLAGS_PERMITTED_BY_ABI ,,
        MAYCHANGE [memory :> bytes(word_sub stackpointer (word 128),128);
                   memory :> bytes(word_add frame (word 144),32);
                   memory :> bytes(z,4 * MIN (LENGTH xs) m)])`;;

let mixed_core_tac image exec copy_mc copy_th bindings
      stackoff after_count copy_call return_copy final_step =
  let stack=`stackpointer:int64` in
  let w n=mk_comb(`word:num->int64`,mk_small_numeral n) in
  let add x y=mk_comb(mk_comb(`word_add:int64->int64->int64`,x),y) in
  let sub x y=mk_comb(mk_comb(`word_sub:int64->int64->int64`,x),y) in
  let allocated=sub (add stack (w stackoff)) (w 128) in
  let scratch=fst(find (fun (_,v)->v=`scratch_bytes:num`) bindings) in
  let contained=vsubst [allocated,`allocated:int64`;scratch,`scratch:num`]
    `contained (word_sub (stackpointer:int64) (word 48),scratch) (allocated,136)` in
  let address=mk_eq(sub stack (w 48),add allocated (w (80-stackoff))) in
    REWRITE_TAC[C_ARGUMENTS; SOME_FLAGS; ALL; NONOVERLAPPING_CLAUSES; fst exec; CESTA_WORDS32] THEN
    REPEAT GEN_TAC THEN DISCH_THEN(REPEAT_TCL CONJUNCTS_THEN ASSUME_TAC) THEN
    SUBGOAL_THEN contained ASSUME_TAC THENL
     [SUBGOAL_THEN address SUBST1_TAC THENL [CONV_TAC WORD_RULE; ALL_TAC] THEN
      MATCH_MP_TAC CESTA_CONTAINED_OFFSET THEN CONV_TAC NUM_REDUCE_CONV; ALL_TAC] THEN
    SUBGOAL_THEN (vsubst [scratch,`scratch:num`]
      `ALL (nonoverlapping (word_sub (stackpointer:int64) (word 48),scratch))
        [(word pc,LENGTH(mixed_image:byte list));(frame,192);(x,4*LENGTH(xs:int32 list));(z,4*m)]`
      |> vsubst [lhs(concl image),`mixed_image:byte list`])
      (fun th -> STRIP_ASSUME_TAC(REWRITE_RULE[ALL; NONOVERLAPPING_CLAUSES] th)) THENL
     [REWRITE_TAC[ALL] THEN REPEAT CONJ_TAC THEN
      MATCH_MP_TAC (SPECL [sub stack (w 48);scratch;allocated;`136`] CESTA_SUBREGION_DISJOINT) THEN
      ASM_REWRITE_TAC[NONOVERLAPPING_CLAUSES; fst exec] THEN NO_TAC; ALL_TAC] THEN
    ABBREV_TAC `n = MIN (LENGTH(xs:int32 list)) m` THEN
    SUBGOAL_THEN `n <= LENGTH(xs:int32 list) /\ n <= m /\ n < 2 EXP 61 /\
                  LENGTH(xs:int32 list) < 2 EXP 64 /\ m < 2 EXP 64`
    STRIP_ASSUME_TAC THENL
     [MP_TAC(ISPECL [`LENGTH(xs:int32 list)`;`m:num`] CESTA_MIN_BOUNDS) THEN
      EXPAND_TAC "n" THEN ASM_ARITH_TAC; ALL_TAC] THEN
    SUBGOAL_THEN `48 <= val(stackpointer:int64)` ASSUME_TAC THENL
     [MP_TAC(ISPECL [`stackpointer:int64`;mk_comb(`word:num->int64`,mk_small_numeral stackoff)] CESTA_ADD_UPPER) THEN
      CONV_TAC(DEPTH_CONV WORD_NUM_RED_CONV) THEN ASM_ARITH_TAC; ALL_TAC] THEN
    SUBGOAL_THEN `40 <= val(word_sub stackpointer (word 8):int64) /\
                  8 <= val(word_sub stackpointer (word 8):int64)`
    STRIP_ASSUME_TAC THENL
     [REWRITE_TAC[VAL_WORD_SUB_CASES] THEN CONV_TAC(DEPTH_CONV WORD_NUM_RED_CONV) THEN
      ASM_SIMP_TAC[ARITH_RULE `48 <= n ==> 8 <= n`] THEN ASM_ARITH_TAC; ALL_TAC] THEN
    VAL_INT64_TAC `LENGTH(xs:int32 list)` THEN VAL_INT64_TAC `m:num` THEN VAL_INT64_TAC `n:num` THEN
    SUBGOAL_THEN `cesta_count_word (word(LENGTH(xs:int32 list))) (word m) = word n`
    ASSUME_TAC THENL [ASM_SIMP_TAC[CESTA_COUNT_NUM] THEN EXPAND_TAC "n" THEN REFL_TAC; ALL_TAC] THEN
    SUBGOAL_THEN `(n = 0 \/ ~(x:int64 = word 0)) /\ val(x:int64) + 4 * n <= 2 EXP 64`
    (CONJUNCTS_THEN ASSUME_TAC) THENL
     [CONJ_TAC THENL
       [ASM_MESON_TAC[ARITH_RULE `n <= m /\ ~(n = 0) ==> ~(m = 0)`]; ASM_ARITH_TAC]; ALL_TAC] THEN
    ENSURES_INIT_TAC "s0" THEN cesta_tisa_steps exec [1] THEN
    X86_SUBROUTINE_SIM_TAC (image,exec,0,prefix_count_mc,PREFIX_COUNT_CORRECT)
      [`frame:int64`;`word(LENGTH(xs:int32 list)):int64`;`word m:int64`;`pc:num`;
       `word_sub stackpointer (word 8):int64`;
       mk_comb(`word:num->int64`,mk_comb(mk_comb(`(+):num->num->num`,`pc:num`),mk_small_numeral after_count))] 2 THEN
    cesta_tisa_normalize THEN
    SUBGOAL_THEN `wordlist_from_memory(x,n) s2 = cesta_prefix n (xs:int32 list)` ASSUME_TAC THENL
     [MATCH_MP_TAC CESTA_PREFIX_FROM_MEMORY THEN ASM_REWRITE_TAC[CESTA_WORDS32]; ALL_TAC] THEN
    RULE_ASSUM_TAC(REWRITE_RULE[CESTA_WORDS32]) THEN
    cesta_tisa_steps exec (3--copy_call) THEN
    X86_SUBROUTINE_SIM_TAC (image,exec,208,copy_mc,copy_th)
      [`x:int64`;`z:int64`;`n:num`;`xs:int32 list`;`m:num`;`pc+208`;
       `word_sub stackpointer (word 8):int64`;
       mk_comb(`word:num->int64`,mk_comb(mk_comb(`(+):num->num->num`,`pc:num`),mk_small_numeral return_copy));
       `read events s`]
      (copy_call+1) THEN
    cesta_tisa_steps exec ((copy_call+2)--final_step) THEN
    ENSURES_FINAL_STATE_TAC THEN ASM_REWRITE_TAC[CESTA_PREFIX_LENGTH; CESTA_WORDS32] THEN
    cesta_stack_frame_tac `stackpointer:int64` 48;;

let mixed_core_rule image exec copy_mc copy_th bindings
      stackoff after_count copy_call return_copy final_step =
  prove(mixed_core_statement bindings,
    mixed_core_tac image exec copy_mc copy_th bindings stackoff after_count copy_call return_copy final_step);;

let mixed_full_rule image exec core entry saved stackoff prologue epilogue =
  prove(mixed_full_statement image entry,
    REWRITE_TAC[C_ARGUMENTS; SOME_FLAGS; ALL; NONOVERLAPPING_CLAUSES;
                MAYCHANGE_REGS_AND_FLAGS_PERMITTED_BY_ABI; fst exec; CESTA_WORDS32] THEN
    REPEAT GEN_TAC THEN DISCH_THEN(REPEAT_TCL CONJUNCTS_THEN ASSUME_TAC) THEN
    MAP_EVERY (fun reg -> ENSURES_PRESERVED_TAC ("initial_" ^ fst(dest_const reg)) reg) saved THEN
    REWRITE_TAC(!simulation_precanon_thms) THEN
    ENSURES_INIT_TAC "s0" THEN cesta_tisa_steps exec (1--prologue) THEN
    MP_TAC(SPECL [`frame:int64`;`x:int64`;`xs:int32 list`;`z:int64`;`m:num`;`pc:num`;
      mk_comb(mk_comb(`word_sub:int64->int64->int64`,`stackpointer:int64`),
              mk_comb(`word:num->int64`,mk_small_numeral stackoff))] core) THEN
    CONV_TAC (DEPTH_CONV cesta_tisa_offset_conv) THEN
    ASM_REWRITE_TAC[C_ARGUMENTS; SOME_FLAGS; ALL; NONOVERLAPPING_CLAUSES; fst exec; CESTA_WORDS32;
                    MAYCHANGE_REGS_AND_FLAGS_PERMITTED_BY_ABI] THEN
    TRY(ANTS_TAC THENL
     [REPEAT CONJ_TAC THEN TRY(FIRST_ASSUM ACCEPT_TAC) THEN
      NONOVERLAPPING_TAC THEN NO_TAC; ALL_TAC]) THEN
    X86_BIGSTEP_TAC exec ("s" ^ string_of_int(prologue+1)) THEN
    cesta_tisa_normalize THEN
    cesta_tisa_steps exec ((prologue+2)--(prologue+epilogue+1)) THEN
    ENSURES_FINAL_STATE_TAC THEN ASM_REWRITE_TAC[CESTA_WORDS32] THEN
    cesta_stack_frame_tac `stackpointer:int64` 128);;

let MIXED_GCC_CORE = mixed_core_rule copy_gcc_image MIXED_GCC_EXEC
  copy_gcc_body_mc COPY_GCC_PREFIX_CORRECT
  (mixed_bindings copy_gcc_image `RBX` `R15` `R14` `RBP` 0x170 0x1b3 56 24)
  56 0x175 10 0x1ae 12;;

let MIXED_GCC_CORRECT = mixed_full_rule copy_gcc_image MIXED_GCC_EXEC
  MIXED_GCC_CORE 0x150 [`R15`;`R14`;`RBP`;`RBX`] 56 12 6;;

let MIXED_CLANG_CORE = mixed_core_rule copy_clang_image MIXED_CLANG_EXEC
  copy_clang_body_mc COPY_CLANG_PREFIX_CORRECT
  (mixed_bindings copy_clang_image `R12` `R15` `R14` `RBX` 0x19a 0x1cb 40 8)
  40 0x19f 11 0x1c5 14;;

let MIXED_CLANG_CORRECT = mixed_full_rule copy_clang_image MIXED_CLANG_EXEC
  MIXED_CLANG_CORE 0x180 [`R15`;`R14`;`R12`;`RBX`] 40 11 6;;
