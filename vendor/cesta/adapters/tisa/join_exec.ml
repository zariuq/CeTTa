(* Lift per-symbol decode theorems into one linked image. Padding stays outside
   the execution rule: a proof cannot step into a gap with no decode theorem. *)

let cesta_join_exec_rule image parts =
  let length_thm=COMPUTE_LENGTH_RULE image in
  let size=dest_small_numeral(rhs(concl length_thm)) in
  let rules=Array.make size None in
  let state=`s:x86state` and pc=`pc:num` in
  let loaded=vsubst [lhs(concl image),`cesta_joined_code:byte list`]
    `bytes_loaded (s:x86state) (word(pc:num)) cesta_joined_code` in
  List.iter (fun (offset,code,(_,exec)) ->
    if offset<0 || offset+Array.length exec>size then failwith "symbol outside image";
    let sub=BYTES_LOADED_SUBPROGRAM_RULE image code offset in
    let at=mk_comb(mk_comb(`(+):num->num->num`,pc),mk_small_numeral offset) in
    let bytes=MATCH_MP (SPECL[state;pc] sub) (ASSUME loaded) in
    Array.iteri (fun i item -> match item with
      | None -> ()
      | Some decoded ->
          if rules.(offset+i)<>None then failwith "overlapping decode slices";
          let step=MATCH_MP (SPECL[state;at] decoded) bytes in
          let step=CONV_RULE (DEPTH_CONV NORMALIZE_ADD_ADD_CONV) step in
          let step=REWRITE_RULE[ADD_CLAUSES] step in
          rules.(offset+i)<-Some(GENL[state;pc](DISCH loaded step))) exec) parts;
  length_thm,rules;;
