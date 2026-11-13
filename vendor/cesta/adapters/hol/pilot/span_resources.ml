(* A finite live-allocation interpretation and the borrowed-view fragment of
   ordered loan permissions. No runtime ledger is introduced. The caller must
   relate this interpretation to its allocator and initialized memory. *)

let cesta_allocations = new_definition
 `cesta_allocations (roots:num list) base capacity initialized <=>
    (!r. MEM r roots ==> initialized r <= capacity r /\
       base r + capacity r <= 2 EXP 64 /\
       (~(capacity r = 0) ==> ~(base r = 0))) /\
    (!r t. MEM r roots /\ MEM t roots /\ ~(r = t)
       ==> nonoverlapping (word(base r):int64,capacity r)
                          (word(base t),capacity t))`;;

let cesta_resolved_span = new_definition
 `cesta_resolved_span (roots:num list) base initialized (generation:num->num) root epoch offset n stride alignment (p:int64) <=>
    MEM root roots /\ epoch = generation root /\ 0 < stride /\
    n * stride <= 2 EXP 63 - 1 /\ offset * stride + n * stride <= initialized root /\
    0 < alignment /\ (base root + offset * stride) MOD alignment = 0 /\
    base root + offset * stride < 2 EXP 64 /\ p = word(base root + offset * stride)`;;

let cesta_loan_ordered = new_definition
 `cesta_loan_ordered (ids:num list) parents <=>
    !i j. MEM i ids /\ MEM j (parents i) ==> j < i`;;

let cesta_loan_access = new_definition
 `cesta_loan_access (ids:num list) (root:num->num) writable parents i writing <=>
    MEM i ids /\ (writing ==> writable i) /\
    (!j. MEM j ids /\ ~(j = i) /\ root j = root i /\ ~MEM j (parents i)
         ==> ~writing /\ ~writable j)`;;

let CESTA_LOAN_EXCLUSIVE_ROOTS = prove
 (`!ids root writable parents i j reading.
     cesta_loan_ordered ids parents /\
     cesta_loan_access ids root writable parents i T /\
     cesta_loan_access ids root writable parents j reading /\ ~(i = j)
     ==> ~(root i = root j)`,
  REWRITE_TAC[cesta_loan_ordered; cesta_loan_access] THEN
  MESON_TAC[LT_ANTISYM]);;

let CESTA_STALE_SPAN_REFUSED = prove
 (`!roots base initialized generation r epoch off n stride alignment p.
     ~(epoch = generation r)
     ==> ~cesta_resolved_span roots base initialized generation r epoch off n stride alignment p`,
  REWRITE_TAC[cesta_resolved_span] THEN MESON_TAC[]);;

let CESTA_RESOLVED_SUBRANGE_CONTROL = prove
 (`cesta_resolved_span [0] (\r. 4096) (\r. 16) (\r. 7)
     0 7 1 3 4 4 (word 4100)`,
  REWRITE_TAC[cesta_resolved_span; MEM] THEN CONV_TAC NUM_REDUCE_CONV);;

let CESTA_SPAN_U32_BOUNDS = prove
 (`!(roots:num list) base capacity initialized (generation:num->num) r epoch offset n p.
     cesta_allocations roots base capacity initialized /\
     cesta_resolved_span roots base initialized generation r epoch offset n 4 4 p
     ==> n < 2 EXP 61 /\ (n = 0 \/ ~(p = word 0)) /\
         word_and p (word 3) = word 0 /\ val p + 4 * n <= 2 EXP 64`,
  REWRITE_TAC[cesta_allocations; cesta_resolved_span] THEN
  REPEAT GEN_TAC THEN STRIP_TAC THEN
  SUBGOAL_THEN `initialized(r:num) <= capacity r /\
    base r + capacity r <= 2 EXP 64 /\
    (~(capacity r = 0) ==> ~(base r = 0))` STRIP_ASSUME_TAC THENL
   [ASM_MESON_TAC[]; ALL_TAC] THEN
  ASM_REWRITE_TAC[] THEN
  ASM_SIMP_TAC[VAL_WORD_EQ; DIMINDEX_64; GSYM VAL_EQ; VAL_WORD_0;
               CESTA_MASK_MOD4] THEN
  REPEAT CONJ_TAC THEN ASM_ARITH_TAC);;

let CESTA_RESOLVED_APART = prove
 (`!(roots:num list) base capacity initialized (generation:num->num) r t er et offR offT n m x z.
     cesta_allocations roots base capacity initialized /\ ~(r = t) /\
     cesta_resolved_span roots base initialized generation r er offR n 4 4 x /\
     cesta_resolved_span roots base initialized generation t et offT m 4 4 z
     ==> nonoverlapping (x,4*n) (z,4*m)`,
  REWRITE_TAC[cesta_allocations; cesta_resolved_span] THEN
  REPEAT GEN_TAC THEN STRIP_TAC THEN
  SUBGOAL_THEN `nonoverlapping (word(base(r:num)):int64,capacity r)
                              (word(base(t:num)),capacity t)` ASSUME_TAC THENL
   [ASM_MESON_TAC[]; ALL_TAC] THEN
  SUBGOAL_THEN `offR * 4 + n * 4 <= capacity(r:num) /\
               offT * 4 + m * 4 <= capacity(t:num)` STRIP_ASSUME_TAC THENL
   [ASM_MESON_TAC[LE_TRANS]; ALL_TAC] THEN
  ASM_REWRITE_TAC[GSYM WORD_ADD] THEN NONOVERLAPPING_TAC);;

let cesta_copy_resources = new_definition
 `cesta_copy_resources ids (root:num->num) writable parents roots base capacity initialized (generation:num->num)
      source destination sourceEpoch destinationEpoch sourceOffset destinationOffset n m x z <=>
    cesta_loan_ordered ids parents /\
    cesta_loan_access ids root writable parents source F /\
    cesta_loan_access ids root writable parents destination T /\ ~(source = destination) /\
    cesta_allocations roots base capacity initialized /\
    cesta_resolved_span roots base initialized generation (root source)
      sourceEpoch sourceOffset n 4 4 x /\
    cesta_resolved_span roots base initialized generation (root destination)
      destinationEpoch destinationOffset m 4 4 z /\ n <= m`;;

let CESTA_COPY_RESOURCE_BOUNDS = prove
 (`!ids root writable parents roots base capacity initialized generation
      source destination sourceEpoch destinationEpoch sourceOffset destinationOffset n m x z.
    cesta_copy_resources ids root writable parents roots base capacity initialized generation
      source destination sourceEpoch destinationEpoch sourceOffset destinationOffset n m x z
    ==> n <= m /\ m < 2 EXP 61 /\ (n = 0 \/ ~(x = word 0)) /\
        (m = 0 \/ ~(z = word 0)) /\
        word_and x (word 3) = word 0 /\ word_and z (word 3) = word 0 /\
        val x + 4*n <= 2 EXP 64 /\ val z + 4*m <= 2 EXP 64 /\
        nonoverlapping (x,4*n) (z,4*m)`,
  REWRITE_TAC[cesta_copy_resources] THEN
  MESON_TAC[CESTA_LOAN_EXCLUSIVE_ROOTS; CESTA_SPAN_U32_BOUNDS; CESTA_RESOLVED_APART]);;

(* Retain code/stack duties explicitly; the source types cannot locate a
   compiler frame or establish separation from the actual executable image. *)
let cesta_resource_call_rule theorem =
  let variables,body = strip_forall(concl theorem) in
  let _,execution = dest_imp body in
  let hypothesis =
   `cesta_copy_resources ids root writable parents roots base capacity initialized generation
      source destination sourceEpoch destinationEpoch sourceOffset destinationOffset
      (LENGTH(xs:int32 list)) m x z /\
    8 <= val(stackpointer:int64) /\
    ALL (nonoverlapping (word_sub stackpointer (word 8),16))
      [(word pc,LENGTH(codeBytes:byte list)); (x,4*LENGTH xs); (z,4*m)] /\
    nonoverlapping (word pc,LENGTH(codeBytes:byte list)) (z,4*m)` in
  (* Extract the exact code term from the existing theorem's precondition. *)
  let original,_ = dest_imp body in
  let bytes = find_term (fun t -> type_of t = `:byte list`) original in
  let hypothesis = vsubst [bytes,`codeBytes:byte list`] hypothesis in
  let statement = list_mk_forall
    (union variables (frees hypothesis),mk_imp(hypothesis,execution)) in
  prove(statement, REPEAT GEN_TAC THEN STRIP_TAC THEN
    MATCH_MP_TAC (SPEC_ALL theorem) THEN
    ASM_MESON_TAC[CESTA_COPY_RESOURCE_BOUNDS]);;

let COPY_GCC_RESOURCE_CORRECT = cesta_resource_call_rule COPY_GCC_CORRECT;;
let COPY_CLANG_RESOURCE_CORRECT = cesta_resource_call_rule COPY_CLANG_CORRECT;;
