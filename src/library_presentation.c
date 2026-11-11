#include "library_presentation.h"
#include "atom.h"
#include "parser.h"
#include "symbol.h"
#include "term_universe.h"
#include "equation_presentation.h"
#include <stdio.h>
#include <stdlib.h>

static bool presentation_equation_head(Atom *atom, SymbolId *head_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], "="))
        return false;
    SymbolId head = atom_head_symbol_id(atom->expr.elems[1]);
    if (head == SYMBOL_ID_NONE)
        return false;
    *head_out = head;
    return true;
}

static bool presentation_declaration_subject(Atom *atom, SymbolId *subject_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], ":") ||
        atom->expr.elems[1]->kind != ATOM_SYMBOL)
        return false;
    *subject_out = atom_symbol_current_id(atom->expr.elems[1]);
    return true;
}

/* The live declarations of `subject` are exactly the library's, in order. */
static bool presentation_declarations_exact(Arena *scratch, Space *program,
                                         SymbolId subject, Atom **library,
                                         size_t library_len) {
    Atom **types = NULL;
    uint32_t ntypes = space_get_declared_types(
        program, scratch, atom_symbol_id(scratch, subject), &types);
    bool exact = ntypes == library_len;
    for (size_t i = 0u; exact && i < library_len; i++)
        exact = atom_alpha_eq(types[i], library[i]->expr.elems[2]);
    free(types);
    return exact;
}

bool cetta_library_presentation_exact(Space *program, const char *path,
                                      const char *const *constructors,
                                      size_t nconstructors,
                                      const char *trace_tag) {
    Arena scratch;
    arena_init(&scratch);
    /* Read with the lane's own reader, as an import of the file reads it
     * into the program's universe. */
    TermUniverse *universe = program->native.universe;
    AtomId *ids = NULL;
    int count = universe ? parse_metta_file_ids(path, universe, &ids) : -1;
    Atom **atoms = count > 0 ? cetta_malloc(sizeof(Atom *) * (size_t)count) : NULL;
    bool admitted = count > 0 && atoms;
    for (int i = 0; admitted && i < count; i++) {
        atoms[i] = term_universe_copy_atom(universe, &scratch, ids[i]);
        admitted = atoms[i] != NULL;
    }
    /* Each head the library defines, with its equations and declarations in
     * file order. */
    for (int i = 0; admitted && i < count; i++) {
        SymbolId head;
        bool equation = presentation_equation_head(atoms[i], &head);
        if (!equation && !presentation_declaration_subject(atoms[i], &head))
            continue;
        bool first = true;
        for (int j = 0; j < i && first; j++) {
            SymbolId earlier;
            if ((presentation_equation_head(atoms[j], &earlier) ||
                 presentation_declaration_subject(atoms[j], &earlier)) &&
                earlier == head)
                first = false;
        }
        if (!first)
            continue;
        size_t equations = 0u, declarations = 0u;
        Atom **eq = cetta_malloc(sizeof(Atom *) * (size_t)count);
        Atom **decl = cetta_malloc(sizeof(Atom *) * (size_t)count);
        for (int j = i; j < count; j++) {
            SymbolId other;
            if (presentation_equation_head(atoms[j], &other) && other == head)
                eq[equations++] = atoms[j];
            else if (presentation_declaration_subject(atoms[j], &other) &&
                     other == head)
                decl[declarations++] = atoms[j];
        }
        if (equations > 0u)
            admitted = cetta_equation_presentation_exact_atoms(
                &scratch, program, head, eq, equations);
        admitted = admitted && presentation_declarations_exact(
            &scratch, program, head, decl, declarations);
        if (!admitted && trace_tag)
            fprintf(stderr, "[%s] presentation differs at %s\n", trace_tag,
                    symbol_bytes(g_symbols, head));
        free(eq);
        free(decl);
    }
    /* The library's data constructors stay data: no equation defines one. */
    for (size_t c = 0u; admitted && c < nconstructors; c++) {
        SymbolId symbol = symbol_intern_cstr(g_symbols, constructors[c]);
        SpaceEquationCursor cursor;
        if (!space_equation_cursor_init(program, symbol, &cursor))
            continue;
        for (;;) {
            SpaceEquationOccurrenceId id;
            SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
            if (step == SPACE_EQUATION_CURSOR_END)
                break;
            SpaceEquationOccurrence occurrence;
            if (step != SPACE_EQUATION_CURSOR_ITEM ||
                !space_equation_occurrence_resolve(id, &occurrence) ||
                atom_head_symbol_id(occurrence.lhs) == symbol ||
                (occurrence.lhs && occurrence.lhs->kind == ATOM_SYMBOL &&
                 atom_symbol_current_id(occurrence.lhs) == symbol)) {
                admitted = false;
                break;
            }
        }
    }
    free(atoms);
    free(ids);
    arena_free(&scratch);
    return admitted;
}
