(* Megalodon's own token stream for a .mg file, one token per line.
   Built from Megalodon's lexer.mll; the native and GSLT readers must print
   the same lines for the same file. *)

open Parser

let quoted value = Printf.sprintf "%S" value

let option_text value =
  match value with
  | None -> "None"
  | Some text -> "(Some " ^ quoted text ^ ")"

let token_text token =
  match token with
  | STRING s -> "(STRING " ^ quoted s ^ ")"
  | QUESTIONSTRING s -> "(QUESTIONSTRING " ^ quoted s ^ ")"
  | NAM s -> "(NAM " ^ quoted s ^ ")"
  | NUM (negative, whole, fraction, exponent) ->
      Printf.sprintf "(NUM %s %s %s %s)"
        (if negative then "True" else "False")
        (quoted whole) (option_text fraction) (option_text exponent)
  | LPAREN -> "LPAREN"
  | RPAREN -> "RPAREN"
  | COMMA -> "COMMA"
  | COLON -> "COLON"
  | DARR -> "DARR"
  | DEQ -> "DEQ"
  | IN -> "IN"
  | MEM -> "MEM"
  | SUBEQ -> "SUBEQ"
  | VBAR -> "VBAR"
  | LCBRACE -> "LCBRACE"
  | RCBRACE -> "RCBRACE"
  | LBRACK -> "LBRACK"
  | RBRACK -> "RBRACK"
  | AT -> "AT"
  | IF -> "IF"
  | THEN -> "THEN"
  | ELSE -> "ELSE"
  | SEMICOLON -> "SEMICOLON"
  | BACKSLASH -> "BACKSLASH"
  | DOLLAR -> "DOLLAR"
  | DOT -> "DOT"
  | OPENCOM -> "OPENCOM"
  | CLOSECOM -> "CLOSECOM"
  | SALT -> "SALT"
  | OPAQUE -> "OPAQUE"
  | TRANSPARENT -> "TRANSPARENT"
  | TREASURE -> "TREASURE"
  | TITLE -> "TITLE"
  | AUTHOR -> "AUTHOR"
  | ADMITTED -> "ADMITTED"
  | ADMIT -> "ADMIT"
  | ABY -> "ABY"
  | SECTION -> "SECTION"
  | END -> "END"
  | LETDEC -> "LETDEC"
  | STYPE -> "STYPE"
  | INDTYPE -> "INDTYPE"
  | PROPTYPE -> "PROPTYPE"
  | VAR -> "VAR"
  | HYP -> "HYP"
  | PARAM -> "PARAM"
  | AXIOM -> "AXIOM"
  | PREFIX -> "PREFIX"
  | POSTFIX -> "POSTFIX"
  | INFIX -> "INFIX"
  | BINDER -> "BINDER"
  | BINDERPLUS -> "BINDERPLUS"
  | NOTATION -> "NOTATION"
  | UNICODE -> "UNICODE"
  | SUPERSCRIPT -> "SUPERSCRIPT"
  | SUBSCRIPT -> "SUBSCRIPT"
  | DEF -> "DEF"
  | THEOREM kind -> "(THEOREM " ^ quoted kind ^ ")"
  | CONJECTURE -> "CONJECTURE"
  | LET -> "LET"
  | ASSUME -> "ASSUME"
  | APPLY -> "APPLY"
  | CLAIM -> "CLAIM"
  | PROVE -> "PROVE"
  | EXACT -> "EXACT"
  | QED -> "QED"
  | TEXT -> "TEXT"
  | SPECCOMM s -> "(SPECCOMM " ^ quoted s ^ ")"

let () =
  if Array.length Sys.argv <> 2 then begin
    prerr_endline "usage: megalodon_reference_token_oracle FILE.mg";
    exit 2
  end;
  let channel = open_in_bin Sys.argv.(1) in
  let lexbuf = Lexing.from_channel channel in
  (try
     while true do
       print_endline (token_text (Lexer.token lexbuf))
     done
   with Lexer.Eof -> ());
  close_in channel
