:- use_module(library(readutil)).

:- prolog_load_context(directory, Here),
   directory_file_path(Here, '../../src/parser.pl', ParserRelative),
   absolute_file_name(ParserRelative, ParserSource),
   ensure_loaded(ParserSource),
   directory_file_path(Here, '../../src/filereader.pl', ReaderRelative),
   absolute_file_name(ReaderRelative, ReaderSource),
   ensure_loaded(ReaderSource).

:- initialization(main, main).

hex_digit(Value, Code) :-
    ( Value < 10 -> Code is 0'0 + Value
    ; Code is 0'a + Value - 10
    ).

write_hex_byte(Byte) :-
    High is Byte >> 4,
    Low is Byte /\ 15,
    hex_digit(High, HighCode),
    hex_digit(Low, LowCode),
    put_code(HighCode),
    put_code(LowCode).

write_hex_string(String) :-
    string_bytes(String, Bytes, utf8),
    maplist(write_hex_byte, Bytes).

canonical_term_string(Term, String) :-
    copy_term(Term, Copy),
    numbervars(Copy, 0, _),
    with_output_to(
        string(String),
        write_term(Copy, [quoted(true), numbervars(true), ignore_ops(true)] )
    ).

write_term_record(Tag, Term) :-
    canonical_term_string(Term, Canonical),
    format('~w\t', [Tag]),
    write_hex_string(Canonical),
    nl.

write_form_record(Tag, Source, Term) :-
    canonical_term_string(Term, Canonical),
    format('form\t~w\t', [Tag]),
    write_hex_string(Source),
    put_code(9),
    write_hex_string(Canonical),
    nl.

normalized_form(form(Source), Tag, Source, Term) :-
    sread(Source, Term),
    ( Term = [=, [Function|Arguments], _], atom(Function) ->
        length(Arguments, _),
        Tag = function
    ; Tag = expression
    ).
normalized_form(runnable(Source), runnable, Source, Term) :-
    sread(Source, Term).

run_mode(form, Input) :-
    read_file_to_string(Input, Source, [encoding(utf8)]),
    sread(Source, Term),
    write_term_record(term, Term).
run_mode(document, Input) :-
    read_file_to_string(Input, Source, [encoding(utf8)]),
    string_codes(Source, SourceCodes),
    strip(SourceCodes, 0, StrippedCodes),
    phrase(top_forms(Forms, 1), StrippedCodes),
    maplist(normalized_form_record, Forms).

normalized_form_record(Form) :-
    normalized_form(Form, Tag, Source, Term),
    write_form_record(Tag, Source, Term).

write_error(Error) :-
    canonical_term_string(Error, Canonical),
    write('error\t'),
    write_hex_string(Canonical),
    nl.

main :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [ModeAtom, InputAtom],
      memberchk(ModeAtom, [form, document]) ->
        format('petta-parser-authority-v1~nmode\t~w~n', [ModeAtom]),
        catch(run_mode(ModeAtom, InputAtom), Error, write_error(Error)),
        writeln(end)
    ; writeln(user_error, 'usage: petta_parser_authority_v1.pl form|document INPUT'),
      halt(2)
    ).
