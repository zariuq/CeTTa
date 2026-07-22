:- use_module(library(crypto)).
:- use_module(library(readutil)).

:- initialization(main, main).

load_authority_sources(Root) :-
    directory_file_path(Root, 'src/parser.pl', Parser),
    directory_file_path(Root, 'src/filereader.pl', Reader),
    load_files([Parser, Reader], [if(true)]).

normalized_form(form(Source), parsed(Tag, Source, Term)) :-
    sread(Source, Term),
    ( Term = [=, [Function|Arguments], _], atom(Function) ->
        length(Arguments, _),
        Tag = function
    ; Tag = expression
    ).
normalized_form(runnable(Source), parsed(runnable, Source, Term)) :-
    sread(Source, Term).

parse_document(Source, ParsedForms) :-
    string_codes(Source, SourceCodes),
    strip(SourceCodes, 0, StrippedCodes),
    phrase(top_forms(Forms, 1), StrippedCodes),
    maplist(normalized_form, Forms, ParsedForms).

canonical_digest(Term, Digest) :-
    copy_term(Term, Copy),
    numbervars(Copy, 0, _),
    with_output_to(
        string(Canonical),
        write_term(Copy, [quoted(true), numbervars(true), ignore_ops(true)])
    ),
    crypto_data_hash(Canonical, Digest, [algorithm(sha256), encoding(utf8)]).

parse_once(Source, ExpectedCount, ObservedCount) :-
    parse_document(Source, ParsedForms),
    length(ParsedForms, ObservedCount),
    ObservedCount =:= ExpectedCount.

repeat_parse(0, _, _, 0) :- !.
repeat_parse(Count, Source, ExpectedCount, TotalCount) :-
    Count > 0,
    parse_once(Source, ExpectedCount, ObservedCount),
    Next is Count - 1,
    repeat_parse(Next, Source, ExpectedCount, Rest),
    TotalCount is ObservedCount + Rest.

nonnegative_integer(Atom, Value) :-
    catch(atom_number(Atom, Value), _, fail),
    integer(Value),
    Value >= 0.

positive_integer(Atom, Value) :-
    nonnegative_integer(Atom, Value),
    Value > 0.

run(Root, Input, WarmupIterations, MeasuredIterations) :-
    load_authority_sources(Root),
    read_file_to_string(Input, Source, [encoding(utf8)]),
    string_bytes(Source, InputBytes, utf8),
    length(InputBytes, InputByteCount),
    parse_document(Source, ParsedForms),
    length(ParsedForms, FormCount),
    canonical_digest(ParsedForms, SemanticDigest),
    repeat_parse(WarmupIterations, Source, FormCount, WarmupTotal),
    get_time(Start),
    repeat_parse(MeasuredIterations, Source, FormCount, MeasuredTotal),
    get_time(Stop),
    ElapsedSeconds is Stop - Start,
    TotalNanoseconds is round(ElapsedSeconds * 1000000000),
    ExpectedWarmupTotal is WarmupIterations * FormCount,
    ExpectedMeasuredTotal is MeasuredIterations * FormCount,
    WarmupTotal =:= ExpectedWarmupTotal,
    MeasuredTotal =:= ExpectedMeasuredTotal,
    format('petta-parser-parse-only-v1~n'),
    format('input-bytes\t~d~n', [InputByteCount]),
    format('forms\t~d~n', [FormCount]),
    format('semantic-sha256\t~w~n', [SemanticDigest]),
    format('warmup-iterations\t~d~n', [WarmupIterations]),
    format('measured-iterations\t~d~n', [MeasuredIterations]),
    format('total-nanoseconds\t~d~n', [TotalNanoseconds]),
    format('end~n').

main :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [Root, Input, WarmupAtom, MeasuredAtom],
      nonnegative_integer(WarmupAtom, WarmupIterations),
      positive_integer(MeasuredAtom, MeasuredIterations) ->
        catch(
            run(Root, Input, WarmupIterations, MeasuredIterations),
            Error,
            ( print_message(error, Error), halt(1) )
        )
    ; writeln(
          user_error,
          'usage: petta_parser_parse_only_v1.pl PETTA_ROOT INPUT WARMUP_ITERATIONS MEASURED_ITERATIONS'
      ),
      halt(2)
    ).
