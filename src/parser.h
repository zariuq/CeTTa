#ifndef CETTA_PARSER_H
#define CETTA_PARSER_H

#include <stdbool.h>
#include "term_universe.h"

/* Parse a .metta file into a list of top-level atoms.
   Returns number of atoms parsed, or -1 on error.
   Atoms are allocated in the provided arena. */
int parse_metta_file(const char *filename, Arena *a, Atom ***out_atoms);

/* Parse MeTTa source text into a list of top-level atoms.
   Returns number of atoms parsed, or -1 on error.
   Atoms are allocated in the provided arena. */
int parse_metta_text(const char *text, Arena *a, Atom ***out_atoms);

/* Parse a .metta file into a list of top-level AtomIds born directly in the
   provided term universe. Returns number of atoms parsed, or -1 on error. */
int parse_metta_file_ids(const char *filename, TermUniverse *universe,
                         AtomId **out_ids);
int parse_metta_file_ids_diagnostic(const char *filename,
                                    TermUniverse *universe,
                                    AtomId **out_ids, char *error_buf,
                                    size_t error_buf_size);

/* Parse MeTTa source text into a list of top-level AtomIds born directly in
   the provided term universe. Returns number of atoms parsed, or -1 on error. */
int parse_metta_text_ids(const char *text, TermUniverse *universe,
                         AtomId **out_ids);
int parse_metta_text_ids_diagnostic(const char *text,
                                    TermUniverse *universe,
                                    AtomId **out_ids, char *error_buf,
                                    size_t error_buf_size);

typedef int (*ParserDocumentTextIdsBackend)(
    void *context, const char *text, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size);
typedef int (*ParserDocumentFileIdsBackend)(
    void *context, const char *filename, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size);

typedef struct {
    void *context;
    ParserDocumentTextIdsBackend parse_text_ids;
    ParserDocumentFileIdsBackend parse_file_ids;
} ParserDocumentIdsBackend;

/* Install one process-wide immutable document reader for the active session. */
bool parser_set_document_ids_backend(
    const ParserDocumentIdsBackend *backend);
void parser_clear_document_ids_backend(void *context);

/*
 * Project a neutral, already-recognized S-expression document into CeTTa's
 * host AtomId representation. Word grounding, namespace normalization,
 * variable co-reference, and profile-controlled rational literals remain
 * owned by the host rather than by a concrete-syntax engine.
 */
bool parser_project_document_ids(Atom *const *atoms, uint32_t atom_len,
                                 TermUniverse *universe, AtomId **out_ids);

/*
 * Incremental host projection for compiled readers.  Concrete-syntax
 * recognition stays in the generated reader while CeTTa retains one
 * implementation of token grounding, namespace normalization, variable
 * identity, and profile-controlled expression lowering.
 *
 * A projection context is bound to one term universe.  Call begin_form at
 * each top-level form boundary: variable spellings co-refer within a form and
 * receive fresh identities in the next form.
 */
typedef struct ParserHostProjectionV1 ParserHostProjectionV1;

ParserHostProjectionV1 *parser_host_projection_v1_new(
    TermUniverse *universe);
void parser_host_projection_v1_free(ParserHostProjectionV1 *projection);
bool parser_host_projection_v1_begin_form(
    ParserHostProjectionV1 *projection);
AtomId parser_host_projection_v1_word_bytes(
    ParserHostProjectionV1 *projection, const uint8_t *bytes, size_t len);
AtomId parser_host_projection_v1_variable_bytes(
    ParserHostProjectionV1 *projection, const uint8_t *bytes, size_t len);
AtomId parser_host_projection_v1_anonymous_variable(
    ParserHostProjectionV1 *projection);
AtomId parser_host_projection_v1_string_bytes(
    ParserHostProjectionV1 *projection, const uint8_t *bytes, size_t len);
AtomId parser_host_projection_v1_expression(
    ParserHostProjectionV1 *projection, const AtomId *children,
    CettaExprLen arity);

/* Parse a single S-expression from a string.
   Advances *pos past the parsed expression.
   Returns NULL on end-of-input or error. */
Atom *parse_sexpr(Arena *a, const char *text, size_t *pos);

/* Parse a single S-expression directly into the term universe and return its
   canonical AtomId. Returns CETTA_ATOM_ID_NONE on end-of-input or error. */
AtomId parse_sexpr_to_id(TermUniverse *universe, const char *text, size_t *pos);

/* Return whether source text has balanced strings/parentheses/comments under
   the reader's delimiter rules. */
bool parser_text_well_formed(const char *text);

/* Advance *pos past delimiters and return whether no non-delimiter text
   remains. */
bool parser_rest_is_delimiters(const char *text, size_t *pos);

/* Canonicalize reader sugar for qualified namespaces.
   This rewrites dotted qualified names such as mork.foo, runtime.bar, and
   import_pkg.moduleA into their canonical colon form before interning.
   Numeric literals, filesystem paths, and known source-file tokens such as
   foo.metta or bench.act must be left unchanged. The returned pointer is
   either the original token or an arena-owned canonical copy. */
const char *parser_canonicalize_namespace_token(Arena *a, const char *tok);

/* Reader option used by he-compat: Rust HE treats slash tokens such as 2/4 as
   ordinary symbols unless an operation explicitly interprets them. */
bool parser_set_rational_literals_enabled(bool enabled);
bool parser_rational_literals_enabled(void);

/* Prime's reflective syntax: @e, *n, and structurally named $@e variables.
   Other language profiles retain their own reader. */
bool parser_set_universal_name_syntax_enabled(bool enabled);
bool parser_universal_name_syntax_enabled(void);

/* Prime's bare `$` is a fresh anonymous variable.  Literal and shared modes
   remain internal tournament controls.  Every mode is deliberately inactive
   unless Prime's universal-name syntax is enabled, so it cannot alter an HE
   reader. */
typedef enum {
    PARSER_BARE_DOLLAR_SYMBOL = 0,
    PARSER_BARE_DOLLAR_FRESH_VARIABLE,
    PARSER_BARE_DOLLAR_SHARED_VARIABLE,
} ParserBareDollarMode;

ParserBareDollarMode parser_set_bare_dollar_mode(ParserBareDollarMode mode);
ParserBareDollarMode parser_bare_dollar_mode(void);

typedef enum {
    PARSER_SYNTAX_QUOTE,
    PARSER_SYNTAX_UNQUOTE,
    PARSER_SYNTAX_VAR,
    PARSER_SYNTAX_REF,
    PARSER_SYNTAX_EXEC,
} ParserSyntaxFormKind;

typedef struct {
    ParserSyntaxFormKind kind;
    const char *compact;
    const char *expanded;
} ParserSyntaxFormSpec;

typedef enum {
    PARSER_SYNTAX_PRINT_COMPACT,
    PARSER_SYNTAX_PRINT_EXPANDED,
} ParserSyntaxPrintMode;

/* One notation ledger is shared by reader lowering and both printers. */
const ParserSyntaxFormSpec *parser_syntax_form_specs(size_t *count_out);

/* Parse one complete source form. A leading top-level ! is represented as
   (syn:exec form), so source directives remain ordinary inspectable data. */
Atom *parser_read_source_form(Arena *a, const char *text);

/* Render reader-admissible syntax. Returns NULL for runtime-only grounded
   handles, whose debug renderings are intentionally not reader syntax. */
char *parser_render_syntax(Arena *a, Atom *atom, ParserSyntaxPrintMode mode);

bool parser_syn_exec_payload(Atom *form, Atom **payload_out);
bool parser_syn_exec_payload_id(const TermUniverse *universe, AtomId form_id,
                                AtomId *payload_out);

#endif /* CETTA_PARSER_H */
