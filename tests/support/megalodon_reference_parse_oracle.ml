open Syntax
open Parser

let print_quoted value = Printf.printf "%S" value

let print_list print_value values =
  print_string "(List";
  List.iter
    (fun value ->
      print_char ' ';
      print_value value)
    values;
  print_char ')'

let print_option print_value value =
  match value with
  | None -> print_string "None"
  | Some item ->
      print_string "(Some ";
      print_value item;
      print_char ')'

let print_bool value = print_string (if value then "True" else "False")
let print_int value = Printf.printf "%d" value

let print_asckind value =
  print_string
    (match value with
    | AscTp -> "AscTp"
    | AscSet -> "AscSet"
    | AscSubeq -> "AscSubeq")

let print_setinfixop value =
  print_string
    (match value with
    | InfMem -> "InfMem"
    | InfSubq -> "InfSubq")

let print_infixop value =
  match value with
  | InfSet operation ->
      print_string "(InfSet ";
      print_setinfixop operation;
      print_char ')'
  | InfNam name ->
      print_string "(InfNam ";
      print_quoted name;
      print_char ')'

let rec print_ltree value =
  match value with
  | ByteL byte -> Printf.printf "(ByteL %d)" byte
  | StringL text ->
      print_string "(StringL ";
      print_quoted text;
      print_char ')'
  | QStringL text ->
      print_string "(QStringL ";
      print_quoted text;
      print_char ')'
  | NaL name ->
      print_string "(NaL ";
      print_quoted name;
      print_char ')'
  | NuL (negative, whole, decimal, exponent) ->
      print_string "(NuL ";
      print_bool negative;
      print_char ' ';
      print_quoted whole;
      print_char ' ';
      print_option print_quoted decimal;
      print_char ' ';
      print_option print_quoted exponent;
      print_char ')'
  | LeL (name, ascription, value, body) ->
      print_string "(LeL ";
      print_quoted name;
      print_char ' ';
      print_option print_ascribed ascription;
      print_char ' ';
      print_ltree value;
      print_char ' ';
      print_ltree body;
      print_char ')'
  | LeML (name, names, value, body) ->
      print_string "(LeML ";
      print_quoted name;
      print_char ' ';
      print_list print_quoted names;
      print_char ' ';
      print_ltree value;
      print_char ' ';
      print_ltree body;
      print_char ')'
  | BiL (name, separator, variables, body) ->
      print_string "(BiL ";
      print_quoted name;
      print_char ' ';
      print_quoted separator;
      print_char ' ';
      print_list print_variable_group variables;
      print_char ' ';
      print_ltree body;
      print_char ')'
  | PreoL (name, argument) ->
      print_string "(PreoL ";
      print_quoted name;
      print_char ' ';
      print_ltree argument;
      print_char ')'
  | PostoL (name, argument) ->
      print_string "(PostoL ";
      print_quoted name;
      print_char ' ';
      print_ltree argument;
      print_char ')'
  | InfoL (operator, left, right) ->
      print_string "(InfoL ";
      print_infixop operator;
      print_char ' ';
      print_ltree left;
      print_char ' ';
      print_ltree right;
      print_char ')'
  | ImplopL (function_, argument) ->
      print_string "(ImplopL ";
      print_ltree function_;
      print_char ' ';
      print_ltree argument;
      print_char ')'
  | SepL (name, operator, set_, predicate) ->
      print_string "(SepL ";
      print_quoted name;
      print_char ' ';
      print_setinfixop operator;
      print_char ' ';
      print_ltree set_;
      print_char ' ';
      print_ltree predicate;
      print_char ')'
  | RepL (name, operator, image, set_) ->
      print_string "(RepL ";
      print_quoted name;
      print_char ' ';
      print_setinfixop operator;
      print_char ' ';
      print_ltree image;
      print_char ' ';
      print_ltree set_;
      print_char ')'
  | SepRepL (name, operator, image, set_, predicate) ->
      print_string "(SepRepL ";
      print_quoted name;
      print_char ' ';
      print_setinfixop operator;
      print_char ' ';
      print_ltree image;
      print_char ' ';
      print_ltree set_;
      print_char ' ';
      print_ltree predicate;
      print_char ')'
  | SetEnumL elements ->
      print_string "(SetEnumL ";
      print_list print_ltree elements;
      print_char ')'
  | MTupleL (first, rest) ->
      print_string "(MTupleL ";
      print_ltree first;
      print_char ' ';
      print_list print_ltree rest;
      print_char ')'
  | ParenL (first, rest) ->
      print_string "(ParenL ";
      print_ltree first;
      print_char ' ';
      print_list print_ltree rest;
      print_char ')'
  | IfThenElseL (condition, then_, else_) ->
      print_string "(IfThenElseL ";
      print_ltree condition;
      print_char ' ';
      print_ltree then_;
      print_char ' ';
      print_ltree else_;
      print_char ')'

and print_ascribed (kind, value) =
  print_char '(';
  print_asckind kind;
  print_char ' ';
  print_ltree value;
  print_char ')'

and print_variable_group (names, ascription) =
  print_char '(';
  print_list print_quoted names;
  print_char ' ';
  print_option print_ascribed ascription;
  print_char ')'

let print_picase value =
  print_string
    (match value with
    | Postfix -> "Postfix"
    | InfixNone -> "InfixNone"
    | InfixLeft -> "InfixLeft"
    | InfixRight -> "InfixRight")

let print_docitem value =
  match value with
  | Author (first, rest) ->
      print_string "(Author ";
      print_quoted first;
      print_char ' ';
      print_list print_quoted rest;
      print_char ')'
  | Title title ->
      print_string "(Title ";
      print_quoted title;
      print_char ')'
  | Salt salt ->
      print_string "(Salt ";
      print_quoted salt;
      print_char ')'
  | Opaque names ->
      print_string "(Opaque ";
      print_list print_quoted names;
      print_char ')'
  | Transparent names ->
      print_string "(Transparent ";
      print_list print_quoted names;
      print_char ')'
  | Treasure treasure ->
      print_string "(Treasure ";
      print_quoted treasure;
      print_char ')'
  | ShowProofTerms enabled ->
      print_string "(ShowProofTerms ";
      print_bool enabled;
      print_char ')'
  | Section name ->
      print_string "(Section ";
      print_quoted name;
      print_char ')'
  | End name ->
      print_string "(End ";
      print_quoted name;
      print_char ')'
  | VarDecl (names, kind, type_) ->
      print_string "(VarDecl ";
      print_list print_quoted names;
      print_char ' ';
      print_asckind kind;
      print_char ' ';
      print_ltree type_;
      print_char ')'
  | LetDecl (name, ascription, value) ->
      print_string "(LetDecl ";
      print_quoted name;
      print_char ' ';
      print_option print_ascribed ascription;
      print_char ' ';
      print_ltree value;
      print_char ')'
  | HypDecl (name, proposition) ->
      print_string "(HypDecl ";
      print_quoted name;
      print_char ' ';
      print_ltree proposition;
      print_char ')'
  | PostInfixDecl (name, semantics, priority, case_) ->
      print_string "(PostInfixDecl ";
      print_quoted name;
      print_char ' ';
      print_ltree semantics;
      Printf.printf " %d " priority;
      print_picase case_;
      print_char ')'
  | PrefixDecl (name, semantics, priority) ->
      print_string "(PrefixDecl ";
      print_quoted name;
      print_char ' ';
      print_ltree semantics;
      Printf.printf " %d)" priority
  | BinderDecl (plus, comma, name, semantics, extra) ->
      print_string "(BinderDecl ";
      print_bool plus;
      print_char ' ';
      print_bool comma;
      print_char ' ';
      print_quoted name;
      print_char ' ';
      print_ltree semantics;
      print_char ' ';
      print_option print_ltree extra;
      print_char ')'
  | NotationDecl (name, arguments) ->
      print_string "(NotationDecl ";
      print_quoted name;
      print_char ' ';
      print_list print_quoted arguments;
      print_char ')'
  | UnicodeDecl (name, codepoints) ->
      print_string "(UnicodeDecl ";
      print_quoted name;
      print_char ' ';
      print_list print_quoted codepoints;
      print_char ')'
  | SubscriptDecl name ->
      print_string "(SubscriptDecl ";
      print_quoted name;
      print_char ')'
  | SuperscriptDecl name ->
      print_string "(SuperscriptDecl ";
      print_quoted name;
      print_char ')'
  | ParamDecl (name, type_) ->
      print_string "(ParamDecl ";
      print_quoted name;
      print_char ' ';
      print_ltree type_;
      print_char ')'
  | DefDecl (name, type_, value) ->
      print_string "(DefDecl ";
      print_quoted name;
      print_char ' ';
      print_option print_ltree type_;
      print_char ' ';
      print_ltree value;
      print_char ')'
  | ParamHash (name, object_hash, proof_hash) ->
      print_string "(ParamHash ";
      print_quoted name;
      print_char ' ';
      print_quoted object_hash;
      print_char ' ';
      print_option print_quoted proof_hash;
      print_char ')'
  | AxDecl (name, proposition) ->
      print_string "(AxDecl ";
      print_quoted name;
      print_char ' ';
      print_ltree proposition;
      print_char ')'
  | ThmDecl (kind, name, proposition) ->
      print_string "(ThmDecl ";
      print_quoted kind;
      print_char ' ';
      print_quoted name;
      print_char ' ';
      print_ltree proposition;
      print_char ')'

let print_named_ltree (name, value) =
  print_char '(';
  print_quoted name;
  print_char ' ';
  print_ltree value;
  print_char ')'

let print_pftacitem value =
  match value with
  | PfStruct depth -> Printf.printf "(PfStruct %d)" depth
  | Exact term ->
      print_string "(Exact ";
      print_ltree term;
      print_char ')'
  | LetTac (names, type_) ->
      print_string "(LetTac ";
      print_list print_quoted names;
      print_char ' ';
      print_option print_ltree type_;
      print_char ')'
  | AssumeTac (names, proposition) ->
      print_string "(AssumeTac ";
      print_list print_quoted names;
      print_char ' ';
      print_option print_ltree proposition;
      print_char ')'
  | SetTac (name, type_, value) ->
      print_string "(SetTac ";
      print_quoted name;
      print_char ' ';
      print_option print_ltree type_;
      print_char ' ';
      print_ltree value;
      print_char ')'
  | ApplyTac term ->
      print_string "(ApplyTac ";
      print_ltree term;
      print_char ')'
  | ClaimTac (name, proposition) ->
      print_string "(ClaimTac ";
      print_quoted name;
      print_char ' ';
      print_ltree proposition;
      print_char ')'
  | ProveTac (first, rest) ->
      print_string "(ProveTac ";
      print_ltree first;
      print_char ' ';
      print_list print_ltree rest;
      print_char ')'
  | CasesTac (term, cases) ->
      print_string "(CasesTac ";
      print_ltree term;
      print_char ' ';
      print_list (print_list print_named_ltree) cases;
      print_char ')'
  | WitnessTac term ->
      print_string "(WitnessTac ";
      print_ltree term;
      print_char ')'
  | RewriteTac (reverse, proof, positions) ->
      print_string "(RewriteTac ";
      print_bool reverse;
      print_char ' ';
      print_ltree proof;
      print_char ' ';
      print_list print_int positions;
      print_char ')'
  | SpecialTac (name, arguments) ->
      print_string "(SpecialTac ";
      print_quoted name;
      print_char ' ';
      print_list print_ltree arguments;
      print_char ')'
  | Qed -> print_string "Qed"
  | Admitted -> print_string "Admitted"
  | Admit -> print_string "Admit"
  | Aby names ->
      print_string "(Aby ";
      print_list print_quoted names;
      print_char ')'

let current_scope_pop = ref (fun () -> ())
let section_scopes : (string * (unit -> unit)) list ref = ref []

let apply_syntax_effect value =
  match value with
  | Section name ->
      section_scopes := (name, !current_scope_pop) :: !section_scopes;
      current_scope_pop := (fun () -> ())
  | End name ->
      begin
        match !section_scopes with
        | (expected, outer_pop) :: rest when name = expected ->
            !current_scope_pop ();
            current_scope_pop := outer_pop;
            section_scopes := rest
        | (expected, _) :: _ ->
            failwith
              ("Section " ^ expected ^ " cannot be ended with End " ^ name)
        | [] -> failwith "No Section To End"
      end
  | PostInfixDecl (name, semantics, priority, case_) ->
      current_scope_pop :=
        declare_postinfix name (ltree_to_atree semantics) priority case_
          !current_scope_pop
  | PrefixDecl (name, semantics, priority) ->
      current_scope_pop :=
        declare_prefix name (ltree_to_atree semantics) priority
          !current_scope_pop
  | BinderDecl (plus, comma, name, semantics, extra) ->
      current_scope_pop :=
        declare_binder plus comma name (ltree_to_atree semantics)
          (Option.map ltree_to_atree extra) !current_scope_pop
  | _ -> ()

(* Megalodon checks each -I signature and then the main file with mgcheck:
   lineno and charno restart, init_env drops open sections without undoing
   them, and the notation tables and the proof state carry over. *)
let proving = ref false

let parse ~print channel =
  lineno := 1;
  charno := 0;
  section_scopes := [];
  current_scope_pop := (fun () -> ());
  let stream = ref (TokStrRest (Lexer.token, Lexing.from_channel channel)) in
  try
    while true do
      if !proving then begin
        let item, rest = parse_pftacitem !stream in
        stream := rest;
        if print then begin
          print_string "(PfTac ";
          print_pftacitem item;
          print_endline ")"
        end;
        (* megalodon.ml leaves proof mode at Qed and Admitted; admit closes
           one goal and the proof goes on. *)
        match item with
        | Qed | Admitted -> proving := false
        | _ -> ()
      end else begin
        let item, rest = parse_docitem !stream in
        stream := rest;
        if print then begin
          print_string "(Doc ";
          print_docitem item;
          print_endline ")"
        end;
        apply_syntax_effect item;
        match item with
        | ThmDecl _ -> proving := true
        | _ -> ()
      end
    done
  with Lexer.Eof -> ()

let () =
  let arguments = Array.to_list (Array.sub Sys.argv 1 (Array.length Sys.argv - 1)) in
  let rec split includes = function
    | "-I" :: signature :: rest -> split (signature :: includes) rest
    | [ file ] -> Some (List.rev includes, file)
    | _ -> None
  in
  match split [] arguments with
  | None ->
      prerr_endline "usage: megalodon_reference_parse_oracle [-I SIGNATURE.mgs]... FILE.mg";
      exit 2
  | Some (includes, file) ->
      let run ~print path =
        let channel = open_in_bin path in
        Fun.protect
          ~finally:(fun () -> close_in_noerr channel)
          (fun () ->
            try parse ~print channel with
            | ParsingError (message, line1, column1, line2, column2) ->
                Printf.eprintf "%sparse error %d:%d-%d:%d: %s\n"
                  (if print then "" else path ^ ": ") line1 column1 line2
                  column2 message;
                exit 1
            | Failure message ->
                Printf.eprintf "%soracle failure: %s\n"
                  (if print then "" else path ^ ": ") message;
                exit 1)
      in
      List.iter (run ~print:false) includes;
      run ~print:true file
