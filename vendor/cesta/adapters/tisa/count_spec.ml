(* Exact execution of the selected scalar artifact. Fuel accounting and its
   frame writes remain part of this theorem; the caller supplies budget zero. *)

let cesta_count_word = new_definition
 `cesta_count_word (a:int64) b = if val a <= val b then a else b`;;

let CESTA_COUNT_VALUE = prove
 (`!(a:int64) b. val(cesta_count_word a b) = MIN (val a) (val b)`,
  REWRITE_TAC[cesta_count_word; MIN] THEN REPEAT GEN_TAC THEN
  COND_CASES_TAC THEN ASM_REWRITE_TAC[]);;

let CESTA_COUNT_BOUNDS = prove
 (`!(a:int64) b.
     val(cesta_count_word a b) <= val a /\
     val(cesta_count_word a b) <= val b`,
  REWRITE_TAC[cesta_count_word] THEN REPEAT GEN_TAC THEN
  COND_CASES_TAC THEN ASM_REWRITE_TAC[LE_REFL] THEN ASM_ARITH_TAC);;

let CESTA_COUNT_NUM = prove
 (`!a b. a < 2 EXP 64 /\ b < 2 EXP 64
    ==> cesta_count_word (word a) (word b) = (word(MIN a b):int64)`,
  REPEAT STRIP_TAC THEN REWRITE_TAC[cesta_count_word; MIN] THEN
  ASM_SIMP_TAC[VAL_WORD_EQ; DIMINDEX_64] THEN
  COND_CASES_TAC THEN ASM_REWRITE_TAC[]);;
