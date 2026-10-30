#ifndef CESTA_H
#define CESTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct CeBlock CeBlock;
typedef struct { CeBlock *blocks; } CeArena;
typedef struct { unsigned char *data; size_t len; } CeText;
typedef struct { char message[512]; size_t offset; } CeError;

void *ce_alloc(CeArena *arena, size_t bytes);
void ce_arena_free(CeArena *arena);
void *ce_resize(void *old, size_t count, size_t size);
CeText ce_text(CeArena *arena, const void *bytes, size_t len);
CeText ce_cstr(CeArena *arena, const char *str);
bool ce_equal(CeText left, CeText right);
bool ce_is(CeText text, const char *str);
bool ce_error(CeError *error, size_t offset, const char *fmt, ...);
/* Compare existing inodes, or canonical parent directories for new outputs. */
bool ce_same_file(const char *a, const char *b);
bool ce_read_file(CeArena *arena, const char *path, CeText *out, CeError *error);
void ce_quote(FILE *file, CeText text);
CeText ce_quoted(CeArena *arena, CeText text);
bool ce_quoted_value(CeArena *arena, CeText source, size_t *position,
                     CeText *value, CeError *error);

/* Ungrounded presentation atoms. Numeric and Boolean spelling is data here. */
enum { CE_ATOM_SYMBOL, CE_ATOM_TEXT, CE_ATOM_LIST };
typedef struct CeAtom {
    unsigned kind;
    CeText text;
    struct CeAtom **items;
    size_t len, offset;
    /* A run of a parent list's items read as a list sort, not a list
       written in the source: its first item is not a head. */
    bool spliced;
} CeAtom;
CeAtom *ce_atom_new(CeArena *arena, unsigned kind, CeText text, size_t len);
bool ce_atom_read(CeArena *arena, CeText source, CeAtom **out, CeError *error);
void ce_atom_write(const CeAtom *atom, FILE *file);
bool ce_atom_equal(const CeAtom *first, const CeAtom *second);
bool ce_wire_pack(CeArena *arena, const CeAtom *atom, CeAtom **out, CeError *error);
bool ce_wire_unpack(CeArena *arena, const CeAtom *wire, CeAtom **out, CeError *error);
bool ce_wire_envelope(const CeAtom *atom);
int ce_wire_command(int argc, char **argv);

typedef struct { CeText text; size_t offset; unsigned kind; } CeMetaToken;
typedef struct {
    CeArena *arena;
    CeText source;
    size_t pos;
    CeMetaToken token;
    CeError *error;
} CeMetaScanner;
enum { CE_META_END, CE_META_NAME, CE_META_STRING, CE_META_CHAR, CE_META_PUNCT };
bool ce_meta_next(CeMetaScanner *scanner);

typedef struct { uint64_t word[4]; } CeByteSet;
typedef struct { CeByteSet bytes; int out[2]; bool epsilon; } CeNfaState;
typedef struct { int start, end; CeByteSet bytes; bool singleton; } CeRegex;
typedef struct { CeText name; int start, end, symbol; bool position, line; } CeLexRule;
enum { CE_NONTERMINAL, CE_LITERAL, CE_TOKEN };
typedef struct {
    CeText name;
    unsigned kind;
    int list_element;
    bool nullable;
    int sort;
    bool virtual_token, erased, identifier_spelling;
    CeText surface;
} CeSymbol;
typedef struct {
    CeText label;
    int lhs;
    int *rhs;
    size_t len;
    size_t offset;
    bool internal;
} CeProduction;
typedef struct { CeText open, close; } CeComment;
typedef struct { CeText production, token, spelling; } CeLexAlias;
typedef struct CeConflictPolicy CeConflictPolicy;
enum {
    CE_SCOPE_PUSH = 1, CE_SCOPE_POP, CE_SCOPE_PROTOTYPE_OPEN,
    CE_SCOPE_PROTOTYPE_CLOSE, CE_SCOPE_CONTEXT_KEEP, CE_SCOPE_DECL_OPEN,
    CE_SCOPE_DECL_CLOSE, CE_SCOPE_BIND_DECLARATOR, CE_SCOPE_BIND_PARAMETER,
    CE_SCOPE_BIND_ENUMERATOR, CE_SCOPE_FUNCTION_OPEN, CE_SCOPE_FUNCTION_CLOSE,
    CE_SCOPE_RECORD_FUNCTION
};
typedef struct { int production; unsigned action; bool after_reduce; } CeScopeHook;
typedef struct { int previous, physical, target; } CeLexPair;
typedef struct { int physical, target; } CeLexSpelling;
typedef struct { int production, wrapper, parent; size_t argument; } CeGrouping;
enum {
    CE_LAYOUT_BLOCK_OPEN = 1, CE_LAYOUT_BLOCK_CLOSE = 2,
    CE_LAYOUT_GROUP_OPEN = 4, CE_LAYOUT_GROUP_CLOSE = 8,
    CE_LAYOUT_LINE = 16, CE_LAYOUT_JOIN = 32,
    CE_LAYOUT_LEFT = 64, CE_LAYOUT_RIGHT = 128, CE_LAYOUT_SPACED = 256,
    CE_LAYOUT_PREFIX = 512
};
typedef struct { int symbol; unsigned flags; } CeLayoutToken;
typedef struct {
    CeArena arena;
    CeSymbol *symbols;
    size_t nsymbols;
    CeProduction *productions;
    size_t nproductions;
    CeLexRule *lex;
    size_t nlex;
    CeNfaState *nfa;
    size_t nnfa;
    CeComment *comments;
    size_t ncomments;
    int *entries;
    size_t nentries;
    CeText name;
    CeLexAlias *aliases;
    size_t naliases;
    CeConflictPolicy *policies;
    size_t npolicies;
    bool c_scope;
    int name_token, variable_terminal, typedef_terminal;
    CeScopeHook *hooks;
    size_t nhooks;
    int *deferred_specifiers;
    size_t ndeferred_specifiers;
    CeLexPair *pairs;
    size_t npairs;
    CeLexSpelling *spellings;
    size_t nspellings;
    CeGrouping *groupings;
    size_t ngroupings;
    /* Literals that attach to the preceding token within listed productions. */
    struct { int symbol, production; } *attachments;
    size_t nattachments;
    CeLayoutToken *layout;
    size_t nlayout, indentation;
    CeText *initial_typedefs;
    size_t ninitial_typedefs;
    /* Presentation surfaces describe atoms, not characters: there "" is a
       zero-width symbol prefix rather than the empty string. */
    bool zero_width_literals;
} CeGrammar;
void ce_grammar_free(CeGrammar *grammar);
int ce_symbol(CeGrammar *grammar, CeText name, unsigned kind);
bool ce_grammar_read(CeGrammar *grammar, CeText source, CeError *error);
bool ce_grammar_finish(CeGrammar *grammar, CeError *error);
bool ce_grammar_finish_signature(CeGrammar *grammar, const CeGrammar *external, CeError *error);
bool ce_profile_read(CeGrammar *grammar, CeText source, CeError *error);
bool ce_profile_load(CeGrammar *grammar, const char *path, CeError *error);
typedef struct { CeText name, source; } CeProfileSource;
bool ce_profile_bundle(CeGrammar *grammar, const CeProfileSource *sources,
                       size_t len, CeText root, CeError *error);
bool ce_regex_match(const CeGrammar *grammar, int start, int end,
                    CeText source, size_t offset, size_t *length);
typedef struct { int symbol; CeText text; size_t offset; } CeToken;
bool ce_token_matches(const CeGrammar *grammar, int production, int expected, const CeToken *token);
typedef struct { CeToken *data; size_t len; } CeTokens;
bool ce_lex(const CeGrammar *grammar, CeText source, CeTokens *out, CeError *error);
void ce_tokens_free(CeTokens *tokens);
void ce_lex_rewrite(const CeGrammar *grammar, CeTokens *tokens);
typedef struct { int transitions[256], symbol; } CeDfaState;
typedef struct { CeDfaState *states; size_t len; } CeDFA;
bool ce_dfa_prepare(const CeGrammar *grammar, CeDFA *out, CeError *error);
void ce_dfa_free(CeDFA *lexer);
bool ce_dfa_lex(const CeGrammar *grammar, const CeDFA *lexer, CeText source,
                 CeTokens *out, CeError *error);

enum { CE_FOREST_SYMBOL, CE_FOREST_PREFIX };
typedef struct { int production, left, right; } CePacked;
typedef struct {
    unsigned kind;
    int symbol, production;
    size_t dot, begin, end;
    CePacked *alternatives;
    size_t len;
    CeText spelling;
} CeForestNode;
typedef struct {
    CeForestNode *nodes;
    size_t len, capacity;
    int root;
    size_t descriptors;
} CeForest;
void ce_forest_free(CeForest *forest);
bool ce_parse(const CeGrammar *grammar, const CeTokens *tokens, int entry,
              CeForest *out, CeError *error);
unsigned ce_forest_count(const CeForest *forest, unsigned ceiling);

enum { CE_LR_SHIFT, CE_LR_REDUCE, CE_LR_ACCEPT };
typedef struct { unsigned kind; int value; } CeAction;
struct CeConflictPolicy { int terminal, next; CeAction preferred, alternative; };
typedef struct { CeAction *actions; size_t len; int selected, policy; } CeActionCell;
typedef struct {
    CeActionCell *cells;
    int *gotos;
    int *defaults;
    size_t states, symbols, conflicts, ambiguities, items;
    int entry;
} CeLR;
void ce_lr_free(CeLR *parser);
bool ce_lr_prepare(const CeGrammar *grammar, int entry, CeLR *out, CeError *error);
bool ce_lr_parse(const CeGrammar *grammar, const CeLR *parser, const CeTokens *tokens,
                 CeForest *out, CeError *error);
void ce_lr_conflicts(const CeGrammar *grammar, const CeLR *parser, FILE *file);

typedef struct CeScope CeScope;
CeScope *ce_scope_new(const CeGrammar *grammar, const CeForest *forest);
void ce_scope_free(CeScope *scope);
bool ce_scope_typedef(const CeScope *scope, CeText name);
bool ce_scope_hook(CeScope *scope, unsigned action, int node, CeError *error);
bool ce_scope_closed(const CeScope *scope, CeError *error);
CeText ce_name_key(CeArena *arena, CeText name);
bool ce_name_equal(CeText first, CeText second);

typedef struct CeTree {
    int symbol, production;
    struct CeTree **children;
    size_t len, offset;
    CeText spelling;
} CeTree;
/* An open source fragment is admitted only by the authoring interface. Closed
   AST codecs and printers never treat its bytes as a parsed C constructor. */
enum { CE_TREE_SOURCE = -3 };
int ce_tree_list_symbol(const CeGrammar *grammar, int required, size_t length);
bool ce_tree_from_forest(const CeGrammar *grammar, const CeForest *forest,
                         const CeTokens *tokens, CeArena *arena,
                         CeTree **out, CeError *error);
void ce_tree_encode(const CeGrammar *grammar, const CeTree *tree, FILE *file);
bool ce_tree_decode(const CeGrammar *grammar, CeText source, int sort,
                     CeArena *arena, CeTree **out, CeError *error);
typedef bool (*CeAtomDecoder)(void *context, const CeAtom *atom, int sort,
                              int parent, CeArena *arena, CeTree **out,
                              CeError *error, size_t depth);
bool ce_tree_decode_atom(const CeGrammar *grammar, const CeAtom *atom, int sort,
                         int parent, CeArena *arena, CeAtomDecoder decode_child,
                         void *context, CeTree **out, CeError *error, size_t depth);
CeAtom *ce_tree_to_atom(const CeGrammar *grammar, const CeTree *tree, CeArena *arena);
bool ce_token_symbol_representation(const CeGrammar *grammar, int symbol);
bool ce_tree_print(const CeGrammar *grammar, const CeTree *tree, int sort,
                    FILE *file, CeError *error);
bool ce_tree_print_prepared(const CeGrammar *grammar, const CeDFA *lexer,
                            const CeTree *tree, int sort, FILE *file, CeError *error);
bool ce_tree_print_open(const CeGrammar *grammar, const CeTree *tree, int sort,
                        FILE *file, CeError *error);
/* A lexer prepared on first use for one grammar and lent to several printers.
   It refuses any other grammar; printers then prepare their own. */
typedef struct {
    const CeGrammar *grammar;
    CeDFA dfa;
    bool attempted, ready;
} CeLazyLexer;
const CeDFA *ce_lazy_lexer(CeLazyLexer *lexer, const CeGrammar *grammar);
void ce_lazy_lexer_free(CeLazyLexer *lexer);
bool ce_tree_print_shared(const CeGrammar *grammar, CeLazyLexer *lexer,
                          const CeTree *tree, int sort, FILE *file, CeError *error);
/* Source mapping for the compile path. The printer starts a line with
   #line N "path" at the statement-level sorts marked in `sorts` (outside
   parentheses) whenever the compiler would otherwise assume another line, and
   records the source offset behind every printed token. Lines and columns are
   1-based; columns count bytes. */
typedef struct { size_t line, column, length, offset; } CeMapToken;
typedef struct { size_t line, presumed; } CeMapDirective;   /* `line` is the first line it governs */
typedef struct {
    CeMapToken *tokens; size_t ntokens, token_capacity;
    CeMapDirective *directives; size_t ndirectives, directive_capacity;
} CeSourceMap;
typedef struct {
    const unsigned char *sorts;
    CeText source;
    const char *path;
    CeSourceMap *map;
} CeLineMarks;
bool ce_tree_print_marked(const CeGrammar *grammar, CeLazyLexer *lexer, const CeTree *tree, int sort,
                          const CeLineMarks *marks, FILE *file, CeError *error);
void ce_source_map_free(CeSourceMap *map);
size_t ce_source_line(CeText source, size_t offset, size_t *column);
bool ce_tree_print_open_shared(const CeGrammar *grammar, CeLazyLexer *lexer,
                               const CeTree *tree, int sort, FILE *file, CeError *error);
bool ce_tree_equal(const CeGrammar *grammar, const CeTree *a, const CeTree *b);
bool ce_tree_label(const CeGrammar *grammar, const CeTree *tree, const char *label);
bool ce_metadata_export(const CeGrammar *grammar, FILE *file, CeError *error);
bool ce_token_value(CeArena *arena, CeText spelling, CeText *out, CeError *error);
int ce_compile_tree(const CeGrammar *grammar, const CeTree *tree, int entry,
                    int argc, char **argv, bool check_only, bool run, CeError *error);
bool ce_tree_emit_checked(const CeGrammar *grammar, const CeTree *tree, int entry,
                          const char *path, CeError *error);
int ce_execute(char *const *argv, CeError *error);
int ce_execute_stdout(char *const *argv, CeArena *arena, CeText *out, CeError *error);
/* Runs argv with one output stream (STDOUT_FILENO or STDERR_FILENO) captured. */
int ce_execute_capture(char *const *argv, int stream, CeArena *arena, CeText *out, CeError *error);
int ce_cc(int argc, char **argv);
int ce_source_command(int argc, char **argv, unsigned composition);
bool ce_builtin_language(CeGrammar *grammar, bool gnu, CeError *error);
bool ce_builtin_dialect(CeGrammar *grammar, bool gnu, bool clang, CeError *error);
int ce_sema_command(int argc, char **argv);

typedef struct { CeTree *tree; uint64_t origin; } CeDefinition;
typedef struct {
    CeDefinition *definitions;
    size_t len;
    CeDefinition *laws;
    size_t nlaws;
    CeTree **exports;
    size_t nexports;
} CePresentation;
typedef struct CeModule CeModule;
enum { CE_COMPOSE_ORIGIN_COHERENT, CE_COMPOSE_ORIGIN_VIEW };
typedef struct {
    CeArena arena;
    const CeGrammar *syntax;
    CeLR syntax_lr;
    bool syntax_prepared, deterministic_syntax;
    CeModule **modules;
    size_t len;
    uint64_t next_origin;
    CeDefinition *origins;
    size_t norigins;
    unsigned composition;
} CeCatalog;
void ce_catalog_free(CeCatalog *catalog);
bool ce_module_load(CeCatalog *catalog, const char *path, CeModule **out, CeError *error);
CeText ce_module_path(const CeModule *module);
CeText ce_module_source(const CeModule *module);
bool ce_module_read(CeCatalog *catalog, CeText identity, CeText source,
                    CeModule **out, CeError *error);
bool ce_module_check(CeCatalog *catalog, CeModule *module, CeError *error);
bool ce_module_theory(CeCatalog *catalog, CeModule *module, const char *name,
                       CePresentation *out, CeError *error);
bool ce_presentation_grammar(CeCatalog *catalog, const CePresentation *presentation,
                             CeGrammar *out, CeError *error);
bool ce_presentation_signature(CeCatalog *catalog, const CePresentation *presentation,
                               const CeGrammar *external, CeGrammar *out, CeError *error);
/* A module law's term as an atom: named and quoted constructors, strings,
   integers and names; implicit substitution and freshness are refused. */
CeAtom *ce_law_atom(const CeGrammar *syntax, CeLazyLexer *lexer, CeArena *arena,
                    const CeTree *tree, CeError *error);

enum { CE_VIEW_SLOT, CE_VIEW_NODE, CE_VIEW_LIST };
typedef struct CeViewTemplate {
    unsigned kind;
    int symbol, production;
    size_t slot, len;
    struct CeViewTemplate **children;
} CeViewTemplate;
enum { CE_VIEW_FORM, CE_VIEW_WORD, CE_VIEW_EMPTY, CE_VIEW_PASS, CE_VIEW_TUPLE, CE_VIEW_LEXICAL, CE_VIEW_PREFIX };
typedef struct { size_t slot; CeText word; } CeViewField;
typedef struct {
    CeText name, head;
    unsigned shape;
    int result;
    int *arguments;
    int *argument_parents;
    size_t arity, offset;
    CeViewField *fields;
    size_t nfields, suffix, shortest;
    bool splice, dotted, admit;
    CeViewTemplate *body;
} CeViewRule;
struct CeViewKeys;
typedef struct {
    CeArena arena;
    const CeGrammar *grammar;
    CeText name;
    unsigned version;
    CeViewRule *rules;
    size_t len;
    struct CeViewKeys *keys;
    uint64_t schema_fingerprint, rules_fingerprint;
} CeViewProfile;
typedef struct {
    uint64_t constructors, shortened, lists, shortened_lists, tokens;
    uint64_t *by_constructor, *short_by_constructor;
    size_t capacity;
} CeViewStats;
void ce_view_free(CeViewProfile *profile);
void ce_view_stats_free(CeViewStats *stats);
bool ce_view_compile(CeViewProfile *profile, const CeGrammar *ast,
                     CeCatalog *catalog, const CePresentation *source,
                     const char *name, unsigned version, CeError *error);
bool ce_view_expand(const CeViewProfile *profile, const CeAtom *surface, int sort,
                    CeArena *arena, CeTree **out, CeViewStats *stats, CeError *error);
enum { CE_EXTENSION_UNHANDLED, CE_EXTENSION_EXPANDED, CE_EXTENSION_ERROR };
typedef bool (*CeViewExtensionAccepts)(void *context, const CeAtom *atom, int sort);
typedef unsigned (*CeViewExtension)(void *context, const CeAtom *atom, int sort,
                                    int parent, CeArena *arena, CeTree **out,
                                    CeAtomDecoder recurse, void *recurse_context,
                                    CeError *error, size_t depth);
bool ce_view_expand_open(const CeViewProfile *profile, const CeAtom *surface, int sort,
                         CeArena *arena, CeTree **out, CeViewExtension extension,
                         CeViewExtensionAccepts accepts, void *context, CeError *error);
bool ce_view_tree(const CeViewProfile *profile, const CeTree *tree, int sort,
                  CeArena *arena, CeAtom **out, CeError *error);
bool ce_view_rules_export(const CeViewProfile *profile, FILE *file, CeError *error);
CeAtom *ce_view_document(const CeViewProfile *profile, int sort, CeAtom *surface, CeArena *arena);
bool ce_view_document_payload(const CeViewProfile *profile, int sort, const CeAtom *document,
                              const CeAtom **out, CeError *error);
int ce_view_command(int argc, char **argv);
bool ce_builtin_view(const CeGrammar *grammar, const char *key, CeViewProfile *out,
                      CeError *error);
bool ce_builtin_decode(const CeGrammar *grammar, CeText source, const char *key,
                        CeArena *arena, CeTree **out, CeError *error);
bool ce_builtin_decode_default(const CeGrammar *grammar, CeText source, const char *key,
                                const char *fallback, CeArena *arena, CeTree **out, CeError *error);

#endif
