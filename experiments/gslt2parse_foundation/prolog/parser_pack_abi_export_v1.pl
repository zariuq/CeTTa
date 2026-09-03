:- module(parser_pack_abi_export_v1,
          [ write_parser_pack_abi_v1/5,
            write_parser_pack_compilation_abi_v1/4
          ]).

:- use_module(parser_pack_eval).
:- use_module(parser_pack_reference_compile).
:- use_module(finite_horn_gslt_v1, [render_term/2]).

:- initialization(main, main).

/*
  A line-framed transport for differential ABI gates.  It carries the existing
  ParserPack streams and their provenance sidecar; it is not another pack root
  or a language authority.  Canonical finite-Horn terms contain no raw tab or
  line-break bytes, so fields remain independently parseable without quoting.
*/

write_parser_pack_abi_v1(PresentationRoot, RelativePaths,
                         StartName, ExpectedClosure, Stream) :-
    parser_pack_reference_compile(
        PresentationRoot, RelativePaths, 4096,
        completed(Compilation)),
    StartState = list([sym('pp-def'), sym(StartName)]),
    write_parser_pack_compilation_abi_v1(
        Compilation, StartState, ExpectedClosure, Stream).

write_parser_pack_compilation_abi_v1(
    parser_pack_compilation(Source, Program, CompilerAnswers, Pack),
    StartState, ExpectedClosure, Stream) :-
    Pack = parser_pack_v1(Productions, ClassClauses),
    CompilerAnswers = compiler_answers(ProductionAnswers, ClassAnswers),
    parser_pack_compilation_provenance(
        Source, Program, CompilerAnswers,
        parser_pack_provenance(
            source_digest(SourceDigest),
            compiler_digest(CompilerDigest),
            environment_digest(EnvironmentDigest),
            production_evidence(ProductionEvidence),
            class_evidence(ClassEvidence))),
    parser_pack_digest(Pack, PackDigest),
    parser_pack_missing_states(Pack, [StartState], MissingStates),
    closure_matches(ExpectedClosure, MissingStates),
    render_wire_term(StartState, StartText),
    format(Stream, 'parser-pack-abi-v1~n', []),
    write_scalar_record(Stream, 'source-digest', SourceDigest),
    write_scalar_record(Stream, 'compiler-digest', CompilerDigest),
    write_scalar_record(Stream, 'environment-digest', EnvironmentDigest),
    write_scalar_record(Stream, 'pack-digest', PackDigest),
    write_scalar_record(Stream, start, StartText),
    write_scalar_record(Stream, closure, ExpectedClosure),
    maplist(write_term_record(Stream, production), Productions),
    maplist(write_term_record(Stream, 'class-clause'), ClassClauses),
    maplist(write_evidence_record(
                Stream, 'production-evidence', ProductionAnswers),
            ProductionEvidence),
    maplist(write_evidence_record(
                Stream, 'class-evidence', ClassAnswers),
            ClassEvidence),
    format(Stream, 'end~n', []).

closure_matches(closed, []).
closure_matches(partial, [_|_]).

write_scalar_record(Stream, Name, Value) :-
    text_string(Value, Text),
    wire_field(Text),
    format(Stream, '~w\t~s~n', [Name, Text]).

write_term_record(Stream, Name, Term) :-
    render_wire_term(Term, Text),
    format(Stream, '~w\t~s~n', [Name, Text]).

write_evidence_record(Stream, Name, Answers,
                      parser_pack_evidence(Artifact, Roots)) :-
    Roots = [_|_],
    render_wire_term(Artifact, ArtifactText),
    forall(member(answer(Answer, Proof), Roots),
           ( memberchk(answer(Answer, Proof), Answers),
             render_wire_term(Answer, AnswerText),
             render_wire_term(Proof, ProofText),
             format(Stream, '~w\t~s\t~s\t~s~n',
                    [Name, ArtifactText, AnswerText, ProofText])
           )).

render_wire_term(Term, Text) :-
    ground(Term),
    render_term(Term, Text),
    wire_field(Text).

text_string(Value, Text) :-
    ( string(Value) -> Text = Value
    ; atom(Value) -> atom_string(Value, Text)
    ).

wire_field(Text) :-
    string_codes(Text, Codes),
    \+ memberchk(9, Codes),
    \+ memberchk(10, Codes),
    \+ memberchk(13, Codes).

main :-
    catch(main_, Error, (print_message(error, Error), halt(1))),
    halt.

% SWI-Prolog 10 passes the script argument vector to `main`; older supported
% releases enter the zero-argument form.  Keep both entry conventions on the
% same implementation so the checked compiler invocation is version-stable.
main(_Arguments) :-
    main.

main_ :-
    current_prolog_flag(argv, Arguments),
    Arguments = [PresentationRoot, StartValue, ClosureValue|RelativePaths],
    RelativePaths = [_|_],
    text_atom(StartValue, StartName),
    text_atom(ClosureValue, ExpectedClosure),
    memberchk(ExpectedClosure, [closed, partial]),
    write_parser_pack_abi_v1(
        PresentationRoot, RelativePaths, StartName, ExpectedClosure,
        current_output).

text_atom(Value, Atom) :-
    ( atom(Value) -> Atom = Value
    ; string(Value) -> atom_string(Atom, Value)
    ).
