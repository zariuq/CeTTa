(* HOL Light revision 924faf0c3471a04174ffdeb8576ce7cabcb92d64. *)
let control_x = mk_var("arrow-control-x",mk_type("bool",[]));;
let control_predicate = mk_abs(control_x,mk_eq(control_x,control_x));;
let control_beta = BETA (mk_comb(control_predicate,control_x));;
let control_witness = EQ_MP (SYM control_beta) (REFL control_x);;
let ARROW_ABS_LAW,ARROW_REP_LAW =
  new_basic_type_definition "->" ("arrow-control-abs","arrow-control-rep") control_witness;;
