(* The selected count supplies the prefix extent required by the copy leaf. *)

let CESTA_ADD_UPPER = prove
 (`!(x:int64) y. val(word_add x y) <= val x + val y`,
  REWRITE_TAC[VAL_WORD_ADD] THEN REPEAT GEN_TAC THEN MATCH_ACCEPT_TAC MOD_LE);;

let CESTA_MIN_BOUNDS = prove
 (`!a b. MIN a b <= a /\ MIN a b <= b`,
  REWRITE_TAC[MIN] THEN REPEAT GEN_TAC THEN COND_CASES_TAC THEN ASM_REWRITE_TAC[LE_REFL] THEN ASM_ARITH_TAC);;

let CESTA_SUBREGION_DISJOINT = prove
 (`!(a:int64) n b m z k.
     contained (a,n) (b,m) /\ nonoverlapping (b,m) (z,k)
     ==> nonoverlapping (a,n) (z,k)`,
  REPEAT STRIP_TAC THEN
  MP_TAC(ISPECL [`b:int64`;`m:num`;`z:int64`;`k:num`;
                `a:int64`;`n:num`;`z:int64`;`k:num`] NONOVERLAPPING_SUBREGIONS) THEN
  ASM_REWRITE_TAC[CESTA_CONTAINED_REFL]);;

let CESTA_PREFIX_LENGTH = prove
 (`!n (xs:int32 list). LENGTH(cesta_prefix n xs) = n`,
  REWRITE_TAC[cesta_prefix; LENGTH_LIST_OF_SEQ]);;

let CESTA_PREFIX_FROM_MEMORY = prove
 (`!(x:int64) n (xs:int32 list) (s:x86state).
     n <= LENGTH xs /\ wordlist_from_memory(x,LENGTH xs) s = xs
     ==> wordlist_from_memory(x,n) s = cesta_prefix n xs`,
  REPEAT STRIP_TAC THEN
  REWRITE_TAC[LIST_EQ; LENGTH_WORDLIST_FROM_MEMORY; CESTA_PREFIX_LENGTH] THEN
  X_GEN_TAC `i:num` THEN DISCH_TAC THEN
  REWRITE_TAC[cesta_prefix] THEN ASM_SIMP_TAC[EL_LIST_OF_SEQ] THEN
  MP_TAC(ISPECL [`x:int64`; `n:num`; `i:num`; `s:x86state`]
    (INST_TYPE [`:4`,`:N`] EL_WORDLIST_FROM_MEMORY)) THEN
  ASM_REWRITE_TAC[DIMINDEX_4; GSYM BYTES32_WBYTES] THEN
  DISCH_THEN SUBST1_TAC THEN
  SUBGOAL_THEN `i < LENGTH(xs:int32 list)` ASSUME_TAC THENL
   [ASM_ARITH_TAC; ASM_MESON_TAC[CESTA_READ32]]);;

let cesta_copy_prefix_rule th =
  let args=[`x:int64`;`z:int64`;`cesta_prefix n (xs:int32 list)`;`m:num`;
            `pc:num`;`stackpointer:int64`;`returnaddress:int64`;`e:uarch_event list`] in
  GENL [`x:int64`;`z:int64`;`n:num`;`xs:int32 list`;`m:num`;`pc:num`;
        `stackpointer:int64`;`returnaddress:int64`;`e:uarch_event list`]
    (REWRITE_RULE[CESTA_PREFIX_LENGTH; CESTA_WORDS32] (SPECL args th));;

let COPY_GCC_PREFIX_CORRECT = cesta_copy_prefix_rule COPY_GCC_CORRECT;;
let COPY_CLANG_PREFIX_CORRECT = cesta_copy_prefix_rule COPY_CLANG_CORRECT;;
