(* The scalar body and its ABI wrapper are proved separately. The standard
   s2n-bignum stack rule accounts for the exact five saved registers. *)
let prefix_count_rule mc exec =
  let instantiate = vsubst [lhs(concl mc),`cesta_profile_code:byte list`] in
  let core = prove(instantiate
 `!frame (a:int64) b pc.
      val frame + 192 <= 2 EXP 64 /\
      nonoverlapping (word pc,LENGTH cesta_profile_code) (frame,192)
      ==> ensures x86
        (\s. bytes_loaded s (word pc) cesta_profile_code /\
             read RIP s = word(pc + 9) /\ C_ARGUMENTS [frame;a;b] s /\
             read (memory :> bytes64(word_add frame (word 160))) s = word 0)
        (\s. read RIP s = word(pc + 187) /\
             read RAX s = cesta_count_word a b /\
             read (memory :> bytes64(word_add frame (word 144))) s = word 1 /\
             read (memory :> bytes64(word_add frame (word 152))) s = cesta_count_word a b /\
             read (memory :> bytes64(word_add frame (word 160))) s = word 0 /\
             read (memory :> bytes64(word_add frame (word 168))) s = word 2)
        (MAYCHANGE [RIP;RAX;R10;R11;R14;R15] ,, MAYCHANGE SOME_FLAGS ,,
         MAYCHANGE [events] ,, MAYCHANGE [memory :> bytes(word_add frame (word 144),32)])`,
    REWRITE_TAC[C_ARGUMENTS; SOME_FLAGS; NONOVERLAPPING_CLAUSES; fst exec] THEN
    REPEAT GEN_TAC THEN STRIP_TAC THEN REWRITE_TAC[cesta_count_word] THEN
    ASM_CASES_TAC `val(a:int64) <= val(b:int64)` THEN ASM_REWRITE_TAC[] THEN
    GHOST_INTRO_TAC `initial_r10:int64` `read R10` THEN
    ENSURES_INIT_TAC "s0" THEN cesta_tisa_steps exec (1--23) THEN
    ENSURES_FINAL_STATE_TAC THEN ASM_REWRITE_TAC[]) in
  prove(instantiate
 `!frame (a:int64) b pc stackpointer returnaddress.
      40 <= val stackpointer /\ val frame + 192 <= 2 EXP 64 /\
      nonoverlapping (word pc,LENGTH cesta_profile_code) (frame,192) /\
      ALL (nonoverlapping (word_sub stackpointer (word 40),48))
        [(word pc,LENGTH cesta_profile_code); (frame,192)]
      ==> ensures x86
        (\s. bytes_loaded s (word pc) cesta_profile_code /\
             read RIP s = word pc /\ read RSP s = stackpointer /\
             read (memory :> bytes64 stackpointer) s = returnaddress /\
             C_ARGUMENTS [frame;a;b] s /\
             read (memory :> bytes64(word_add frame (word 160))) s = word 0)
        (\s. read RIP s = returnaddress /\
             read RSP s = word_add stackpointer (word 8) /\
             read RAX s = cesta_count_word a b /\
             read (memory :> bytes64(word_add frame (word 144))) s = word 1 /\
             read (memory :> bytes64(word_add frame (word 152))) s = cesta_count_word a b /\
             read (memory :> bytes64(word_add frame (word 160))) s = word 0 /\
             read (memory :> bytes64(word_add frame (word 168))) s = word 2)
        (MAYCHANGE [RSP] ,, MAYCHANGE_REGS_AND_FLAGS_PERMITTED_BY_ABI ,,
         MAYCHANGE [memory :> bytes(word_sub stackpointer (word 40),40);
                    memory :> bytes(word_add frame (word 144),32)])`,
    X86_ADD_RETURN_STACK_TAC exec core `[RBX;R12;R13;R14;R15]` 40);;

let PREFIX_COUNT_CORRECT = prefix_count_rule prefix_count_mc PREFIX_COUNT_EXEC;;
print_endline "PREFIX_COUNT_RULE_ACCEPTED";;
