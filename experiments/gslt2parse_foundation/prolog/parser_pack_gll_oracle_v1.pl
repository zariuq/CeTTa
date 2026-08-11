:- module(parser_pack_gll_oracle_v1, []).

:- use_module(test_parser_pack_gll, [parser_pack_gll_real_rows/2]).
:- use_module(finite_horn_gslt_v1, [render_term/2]).

:- initialization(main, main).

main :-
    catch(main_, Error, (print_message(error, Error), halt(1))),
    halt.

main_ :-
    current_prolog_flag(argv, [PresentationRoot]),
    parser_pack_gll_real_rows(PresentationRoot, Rows),
    format('parser-pack-gll-oracle-v1~n', []),
    maplist(write_row, Rows),
    format('end~n', []).

write_row(real_row(Language, Label, Decision, Results, ForestDigest)) :-
    format('case\t~w\t~w\t~w\t~w~n',
           [Language, Label, Decision, ForestDigest]),
    maplist(write_result, Results),
    format('end-case~n', []).

write_result(result(Value, Rest)) :-
    render_term(list([sym(result), Value, Rest]), Text),
    format('result\t~s~n', [Text]).
