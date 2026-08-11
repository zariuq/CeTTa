:- use_module(library(readutil)).
:- use_module(library(dcg/basics),
              [blanks//0, blanks_to_nl//0, eos//0, string_without//2]).

:- prolog_load_context(directory, Here),
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

write_form(form(Source)) :-
    write('form\t'),
    write_hex_string(Source),
    nl.
write_form(runnable(Source)) :-
    write('runnable\t'),
    write_hex_string(Source),
    nl.

write_error(Error) :-
    copy_term(Error, Copy),
    numbervars(Copy, 0, _),
    with_output_to(
        string(Canonical),
        write_term(Copy, [quoted(true), numbervars(true), ignore_ops(true)])
    ),
    write('error\t'),
    write_hex_string(Canonical),
    nl.

run(Input) :-
    read_file_to_string(Input, Source, [encoding(utf8)]),
    string_codes(Source, SourceCodes),
    strip(SourceCodes, 0, StrippedCodes),
    phrase(top_forms(Forms, 1), StrippedCodes),
    maplist(write_form, Forms).

main :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [Input] ->
        writeln('petta-document-splitter-authority-v1'),
        catch(run(Input), Error, write_error(Error)),
        writeln(end)
    ; writeln(user_error,
              'usage: petta_document_splitter_authority_v1.pl INPUT'),
      halt(2)
    ).
