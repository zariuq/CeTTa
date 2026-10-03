#include "prime_level.h"
#include "prime_regular_kernel.h"
#include "prime_regular_pattern.h"
#include "symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Verdicts of the level operations on the cases of
 * generate_prime_level_differential.py, one line per case, for comparison
 * with the verdicts the Lean definitions give on the same cases.
 *
 * A notation travels as its prefix code: 0 for zero, and for
 * omega^e * c + r the coefficient c followed by the code of e and the code
 * of r.  A number has any number of digits.  A level expression travels in
 * prefix form: 0 and a notation code for a constant, 1 and an index for a
 * parameter, 2 for a successor, 3 for a maximum, 4 and a number n for the
 * n-th level above the Cantor normal forms, and 5 and a count for that many
 * successors.  An arithmetic expression over numerals and omega travels in
 * prefix form: 0 and a number for a numeral, 4 for omega, and 1, 2 or 3
 * followed by two expressions for a sum, a product or a power.  A level that
 * may lie above the Cantor normal forms travels as 0 and a notation code, or
 * as 1 and the number n of the n-th level above them.  Fields of a case are
 * separated by `;`.
 *
 * Some verdicts come from more than the level library.  A tree is also
 * written as a constant of the kernel's core syntax, and the kernel's own
 * test of that syntax is printed beside the library's.  The maximum of two
 * notations is also read from `(max a b)` as an author writes it.  An
 * arithmetic expression is read as an author writes it inside `(u ...)`.
 * Every notation is printed as an author writes it and read back, which
 * must give the same kernel constant.  And of two levels that may lie above
 * the Cantor normal forms the kernel is asked whether the universe at the
 * first is a member of the universe at the second.
 *
 * A written pair is two level expressions over the levels above the Cantor
 * normal forms and a valuation of the parameters 0, 1 and 2 in the Cantor
 * normal forms.  A parameter stands for a level an author writes, so the
 * order is the one under every such valuation.  The verdict is the canonical
 * forms of the two, of the successor of the first and of their maximum, the
 * order each way and the equality, and the values of the two at the
 * valuation.  The kernel is asked whether the universe at the first is a
 * member of the universe at the successor of the second, which holds exactly
 * when the first is at most the second.
 *
 * A raise is one step of the kernel's search for the least level instance:
 * three parameters hold closed levels and are raised so that a level
 * expression reaches a closed bound.  The verdict is what they hold
 * afterwards, or that no least raise exists. */

typedef struct {
    const char **numbers;
    size_t count;
    size_t position;
} Field;

typedef struct {
    const CettaPrimeLevelV1 *levels[3];
} Substitution;

typedef struct {
    const CettaPrimeLevelNotationV1 *values[3];
} Valuation;

static void fail(const char *message, size_t line) {
    fprintf(stderr, "prime_level_differential: line %zu: %s\n", line, message);
    exit(2);
}

static bool field_next(Field *field, const char **number_out) {
    if (field->position == field->count) return false;
    *number_out = field->numbers[field->position++];
    return true;
}

/* The next number of a field as a machine number: a tag or an index. */
static bool field_next_small(Field *field, uint64_t *number_out) {
    const char *digits = NULL;
    char *end = NULL;
    if (!field_next(field, &digits) || strlen(digits) > 18u) return false;
    *number_out = strtoull(digits, &end, 10);
    return end && *end == '\0';
}

static bool number_is_zero(const char *digits) {
    while (*digits == '0') digits++;
    return *digits == '\0';
}

/* Read one notation.  `normal` turns false when some node of the tree is
 * not a normal form; the whole tree is still consumed. */
static bool read_notation(
    Arena *arena, Field *field, const CettaPrimeLevelNotationV1 **notation_out,
    bool *normal) {
    const char *digits = NULL;
    *notation_out = NULL;
    if (!field_next(field, &digits)) return false;
    if (number_is_zero(digits)) return true;
    const CettaPrimeLevelNaturalV1 *coefficient = NULL;
    const CettaPrimeLevelNotationV1 *exponent = NULL;
    const CettaPrimeLevelNotationV1 *remainder = NULL;
    if (cetta_prime_level_natural_from_decimal_v1(
            arena, digits, &coefficient) != CETTA_PRIME_LEVEL_OK_V1 ||
        !read_notation(arena, field, &exponent, normal) ||
        !read_notation(arena, field, &remainder, normal))
        return false;
    if (!*normal) return true;
    CettaPrimeLevelStatusV1 status = cetta_prime_level_notation_v1(
        arena, exponent, coefficient, remainder, notation_out);
    if (status == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1) {
        *normal = false;
        return true;
    }
    return status == CETTA_PRIME_LEVEL_OK_V1;
}

/* Read one level that may lie above the Cantor normal forms: 0 and a
 * notation in normal form, or 1 and the number of a level above them. */
static bool read_extended(
    Arena *arena, Field *field,
    const CettaPrimeLevelNotationV1 **notation_out) {
    uint64_t tag = 0u;
    const char *digits = NULL;
    const CettaPrimeLevelNaturalV1 *number = NULL;
    bool normal = true;
    *notation_out = NULL;
    if (!field_next_small(field, &tag)) return false;
    if (tag == 0u)
        return read_notation(arena, field, notation_out, &normal) && normal;
    return tag == 1u && field_next(field, &digits) &&
           cetta_prime_level_natural_from_decimal_v1(
               arena, digits, &number) == CETTA_PRIME_LEVEL_OK_V1 &&
           cetta_prime_level_notation_above_v1(arena, number, notation_out) ==
               CETTA_PRIME_LEVEL_OK_V1;
}

static bool read_level(
    Arena *arena, Field *field, const CettaPrimeLevelV1 **level_out) {
    uint64_t tag = 0u;
    *level_out = NULL;
    if (!field_next_small(field, &tag)) return false;
    if (tag == 0u) {
        const CettaPrimeLevelNotationV1 *constant = NULL;
        bool normal = true;
        return read_notation(arena, field, &constant, &normal) && normal &&
               cetta_prime_level_constant_v1(arena, constant, level_out) ==
                   CETTA_PRIME_LEVEL_OK_V1;
    }
    if (tag == 1u) {
        uint64_t parameter = 0u;
        return field_next_small(field, &parameter) &&
               cetta_prime_level_parameter_v1(arena, parameter, level_out) ==
                   CETTA_PRIME_LEVEL_OK_V1;
    }
    if (tag == 2u) {
        const CettaPrimeLevelV1 *inner = NULL;
        return read_level(arena, field, &inner) &&
               cetta_prime_level_successor_v1(arena, inner, level_out) ==
                   CETTA_PRIME_LEVEL_OK_V1;
    }
    if (tag == 3u) {
        const CettaPrimeLevelV1 *left = NULL;
        const CettaPrimeLevelV1 *right = NULL;
        return read_level(arena, field, &left) &&
               read_level(arena, field, &right) &&
               cetta_prime_level_maximum_v1(arena, left, right, level_out) ==
                   CETTA_PRIME_LEVEL_OK_V1;
    }
    if (tag == 4u || tag == 5u) {
        const char *digits = NULL;
        const CettaPrimeLevelNaturalV1 *number = NULL;
        const CettaPrimeLevelNotationV1 *above = NULL;
        const CettaPrimeLevelV1 *inner = NULL;
        if (!field_next(field, &digits) ||
            cetta_prime_level_natural_from_decimal_v1(
                arena, digits, &number) != CETTA_PRIME_LEVEL_OK_V1)
            return false;
        if (tag == 4u)
            return cetta_prime_level_notation_above_v1(
                       arena, number, &above) == CETTA_PRIME_LEVEL_OK_V1 &&
                   cetta_prime_level_constant_v1(arena, above, level_out) ==
                       CETTA_PRIME_LEVEL_OK_V1;
        return read_level(arena, field, &inner) &&
               cetta_prime_level_offset_v1(arena, inner, number, level_out) ==
                   CETTA_PRIME_LEVEL_OK_V1;
    }
    return false;
}

/* The tree of a code as a constant of the kernel's core syntax, whether or
 * not it is a normal form: `(LevelConst n)` for a natural number alone, and
 * `(LevelCantor exponent coefficient remainder)` for any other tree. */
static Atom *read_core_constant(Arena *arena, Field *field) {
    const char *digits = NULL;
    if (!field_next(field, &digits)) return NULL;
    Atom *zero = atom_expr2(
        arena, atom_symbol(arena, "LevelConst"), atom_int(arena, 0));
    if (number_is_zero(digits)) return zero;
    Atom *coefficient = atom_bigint(arena, digits);
    Atom *exponent = read_core_constant(arena, field);
    Atom *remainder = exponent ? read_core_constant(arena, field) : NULL;
    if (!coefficient || !remainder) return NULL;
    if (atom_eq(exponent, zero) && atom_eq(remainder, zero))
        return atom_expr2(
            arena, atom_symbol(arena, "LevelConst"), coefficient);
    Atom *items[4] = {
        atom_symbol(arena, "LevelCantor"), exponent, coefficient, remainder,
    };
    return atom_expr(arena, items, 4u);
}

/* The same for a level that may lie above the Cantor normal forms, where
 * the n-th level above them is `(LevelAbove n)`. */
static Atom *read_core_extended(Arena *arena, Field *field) {
    uint64_t tag = 0u;
    const char *digits = NULL;
    if (!field_next_small(field, &tag)) return NULL;
    if (tag == 0u) return read_core_constant(arena, field);
    Atom *number = tag == 1u && field_next(field, &digits)
        ? atom_bigint(arena, digits) : NULL;
    return number
        ? atom_expr2(arena, atom_symbol(arena, "LevelAbove"), number)
        : NULL;
}

/* A level expression of a code in the kernel's core syntax: a constant, a
 * parameter, a successor, a maximum, a level above the Cantor normal forms
 * or a counted successor. */
static Atom *read_core_level(Arena *arena, Field *field) {
    uint64_t tag = 0u;
    if (!field_next_small(field, &tag)) return NULL;
    if (tag == 0u) return read_core_constant(arena, field);
    if (tag == 1u) {
        uint64_t parameter = 0u;
        return field_next_small(field, &parameter) &&
                       parameter <= (uint64_t)INT64_MAX
            ? atom_expr2(
                  arena, atom_symbol(arena, "LevelParam"),
                  atom_int(arena, (int64_t)parameter))
            : NULL;
    }
    if (tag == 2u) {
        Atom *inner = read_core_level(arena, field);
        return inner
            ? atom_expr2(arena, atom_symbol(arena, "LevelSucc"), inner)
            : NULL;
    }
    if (tag == 3u) {
        Atom *left = read_core_level(arena, field);
        Atom *right = left ? read_core_level(arena, field) : NULL;
        return right
            ? atom_expr3(arena, atom_symbol(arena, "LevelMax"), left, right)
            : NULL;
    }
    if (tag == 4u || tag == 5u) {
        const char *digits = NULL;
        Atom *number =
            field_next(field, &digits) ? atom_bigint(arena, digits) : NULL;
        if (!number) return NULL;
        if (tag == 4u)
            return atom_expr2(arena, atom_symbol(arena, "LevelAbove"), number);
        Atom *inner = read_core_level(arena, field);
        return inner
            ? atom_expr3(
                  arena, atom_symbol(arena, "LevelOffset"), inner, number)
            : NULL;
    }
    return NULL;
}

/* A numeral of the core syntax, printed with every digit. */
static bool print_core_numeral(const Atom *numeral) {
    if (!cetta_prime_regular_kernel_level_numeral_v1(numeral)) return false;
    if (numeral->ground.gkind == GV_INT)
        printf("%" PRId64, numeral->ground.ival);
    else
        printf("%s", atom_bigint_cstr(numeral));
    return true;
}

/* The code of a closed level constant of the core syntax; nothing held is
 * zero. */
static bool print_core_constant(Atom *constant) {
    if (!constant) {
        printf("0");
        return true;
    }
    if (constant->kind != ATOM_EXPR || constant->expr.len < 2u ||
        !constant->expr.elems[0])
        return false;
    if (atom_is_symbol(constant->expr.elems[0], "LevelConst") &&
        constant->expr.len == 2u) {
        Atom *numeral = constant->expr.elems[1];
        if (!print_core_numeral(numeral)) return false;
        if (numeral->ground.gkind != GV_INT || numeral->ground.ival != 0)
            printf(" 0 0");
        return true;
    }
    if (atom_is_symbol(constant->expr.elems[0], "LevelCantor") &&
        constant->expr.len == 4u) {
        if (!print_core_numeral(constant->expr.elems[2])) return false;
        printf(" ");
        if (!print_core_constant(constant->expr.elems[1])) return false;
        printf(" ");
        return print_core_constant(constant->expr.elems[3]);
    }
    return false;
}

/* Print the universe at a core constant as an author writes it, read the
 * printed term back, and return whether the same kernel term results. */
static bool universe_reads_back(Arena *arena, Atom *constant) {
    Atom *universe = atom_expr2(arena, atom_symbol(arena, "Sort"), constant);
    Atom *written =
        cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
            arena, universe);
    if (!written) return false;
    CettaPrimeRegularKernelBudget budget;
    cetta_prime_regular_kernel_budget_init(&budget, false, 0u);
    CettaPrimeRegularTermElaborationV1 lowered =
        cetta_prime_regular_term_to_pattern_v1(arena, written, &budget);
    if (lowered.status != CETTA_PRIME_REGULAR_TERM_OK) return false;
    CettaPrimeRegularPatternElaborationV1 elaborated =
        cetta_prime_regular_pattern_elaborate_v1(
            arena, (CettaPrimeRegularPatternEnvironmentV1){0},
            lowered.pattern, &budget);
    return elaborated.status == CETTA_PRIME_REGULAR_PATTERN_OK &&
           atom_eq(elaborated.term, universe);
}

/* A closed universe as an author writes it, or NULL. */
static Atom *written_level(Arena *arena, Atom *constant) {
    Atom *written = cetta_prime_regular_kernel_quote_closed_universe_sort_v1(
        arena, atom_expr2(arena, atom_symbol(arena, "Sort"), constant));
    return written ? written->expr.elems[1] : NULL;
}

/* An arithmetic expression over numerals and omega as an author writes it,
 * or NULL. */
static Atom *read_arithmetic(Arena *arena, Field *field) {
    static const char *const operators[] = {"+", "*", "^"};
    uint64_t tag = 0u;
    if (!field_next_small(field, &tag)) return NULL;
    if (tag == 0u) {
        const char *digits = NULL;
        return field_next(field, &digits) ? atom_bigint(arena, digits) : NULL;
    }
    if (tag == 4u) return atom_symbol(arena, "omega");
    if (tag > 3u) return NULL;
    Atom *left = read_arithmetic(arena, field);
    Atom *right = left ? read_arithmetic(arena, field) : NULL;
    return right
        ? atom_expr3(
              arena, atom_symbol(arena, operators[tag - 1u]), left, right)
        : NULL;
}

static void print_natural(
    Arena *arena, const CettaPrimeLevelNaturalV1 *natural, size_t line) {
    const char *digits = NULL;
    if (cetta_prime_level_natural_decimal_v1(arena, natural, &digits) !=
        CETTA_PRIME_LEVEL_OK_V1)
        fail("a number has no decimal digits", line);
    printf("%s", digits);
}

/* The code of a Cantor normal form. */
static void print_notation(
    Arena *arena, const CettaPrimeLevelNotationV1 *notation, size_t line) {
    CettaPrimeLevelNotationTermV1 term = {0};
    if (cetta_prime_level_notation_above_value_v1(notation, NULL))
        fail("a level above the Cantor normal forms has no code", line);
    if (!cetta_prime_level_notation_term_v1(notation, &term)) {
        printf("0");
        return;
    }
    print_natural(arena, term.coefficient, line);
    printf(" ");
    print_notation(arena, term.exponent, line);
    printf(" ");
    print_notation(arena, term.remainder, line);
}

/* A level that may lie above the Cantor normal forms: which kind it is,
 * and its code or its number. */
static void print_extended(
    Arena *arena, const CettaPrimeLevelNotationV1 *notation, size_t line) {
    const CettaPrimeLevelNaturalV1 *number = NULL;
    if (cetta_prime_level_notation_above_value_v1(notation, &number)) {
        printf("1 ");
        print_natural(arena, number, line);
        return;
    }
    printf("0 ");
    print_notation(arena, notation, line);
}

static void print_level(
    Arena *arena, const CettaPrimeLevelV1 *level, size_t line) {
    CettaPrimeLevelViewV1 view = {0};
    if (!cetta_prime_level_view_v1(level, &view)) {
        printf("?");
        return;
    }
    print_notation(arena, view.constant, line);
    printf("/");
    for (size_t index = 0u; index < view.parameter_count; index++) {
        printf(
            "%s%" PRIu64 "+", index == 0u ? "" : ",",
            view.parameters[index].parameter);
        print_natural(arena, view.parameters[index].offset, line);
    }
}

/* The same for a level whose constant may lie above the Cantor normal
 * forms. */
static void print_written_level(
    Arena *arena, const CettaPrimeLevelV1 *level, size_t line) {
    CettaPrimeLevelViewV1 view = {0};
    if (!cetta_prime_level_view_v1(level, &view))
        fail("a level has no canonical form", line);
    print_extended(arena, view.constant, line);
    printf("/");
    for (size_t index = 0u; index < view.parameter_count; index++) {
        printf(
            "%s%" PRIu64 "+", index == 0u ? "" : ",",
            view.parameters[index].parameter);
        print_natural(arena, view.parameters[index].offset, line);
    }
}

static CettaPrimeLevelStatusV1 substitute(
    void *context, uint64_t parameter,
    const CettaPrimeLevelV1 **replacement_out) {
    Substitution *substitution = context;
    if (parameter >= 3u) return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    *replacement_out = substitution->levels[parameter];
    return CETTA_PRIME_LEVEL_OK_V1;
}

static CettaPrimeLevelStatusV1 valuate(
    void *context, uint64_t parameter,
    const CettaPrimeLevelNotationV1 **value_out) {
    Valuation *valuation = context;
    if (parameter >= 3u) return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    *value_out = valuation->values[parameter];
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* Split the text after the case letter into fields of numbers.  A number
 * stays the text it is, which has any number of digits. */
static size_t split_fields(
    char *text, Field *fields, size_t capacity, size_t line) {
    size_t count = 0u;
    char *cursor = text;
    while (cursor) {
        char *end = strchr(cursor, ';');
        if (end) *end = '\0';
        if (count == capacity) fail("too many fields", line);
        Field *field = &fields[count++];
        size_t room = strlen(cursor) / 2u + 1u;
        field->numbers = malloc(room * sizeof(*field->numbers));
        if (!field->numbers) fail("out of memory", line);
        field->count = 0u;
        field->position = 0u;
        char *token = cursor;
        while (*token) {
            while (*token == ' ') *token++ = '\0';
            if (!*token) break;
            field->numbers[field->count++] = token;
            if (*token < '0' || *token > '9') fail("not a number", line);
            while (*token >= '0' && *token <= '9') token++;
            if (*token && *token != ' ') fail("not a number", line);
        }
        cursor = end ? end + 1 : NULL;
    }
    return count;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s CASES\n", argv[0]);
        return 2;
    }
    FILE *cases = fopen(argv[1], "rb");
    if (!cases) {
        perror(argv[1]);
        return 2;
    }
    if (fseek(cases, 0L, SEEK_END) != 0) fail("cannot read the cases", 0u);
    long length = ftell(cases);
    if (length < 0 || fseek(cases, 0L, SEEK_SET) != 0)
        fail("cannot read the cases", 0u);
    char *contents = malloc((size_t)length + 1u);
    if (!contents ||
        fread(contents, 1u, (size_t)length, cases) != (size_t)length)
        fail("cannot read the cases", 0u);
    contents[length] = '\0';
    fclose(cases);

    Arena arena;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    size_t line = 0u;
    for (char *text = contents; text && *text;) {
        char *next = strchr(text, '\n');
        if (next) *next++ = '\0';
        line++;
        if (text[0] == '\0') {
            text = next;
            continue;
        }
        Field fields[5] = {0};
        size_t count = split_fields(text + 1, fields, 5u, line);
        bool complete = true;
        switch (text[0]) {
        case 'N': {
            const CettaPrimeLevelNotationV1 *notations[2] = {NULL, NULL};
            const CettaPrimeLevelV1 *levels[2] = {NULL, NULL};
            bool normal = true;
            if (count != 2u) fail("a notation pair has two fields", line);
            for (size_t index = 0u; index < 2u; index++)
                if (!read_notation(
                        &arena, &fields[index], &notations[index], &normal) ||
                    !normal ||
                    fields[index].position != fields[index].count ||
                    cetta_prime_level_constant_v1(
                        &arena, notations[index], &levels[index]) !=
                        CETTA_PRIME_LEVEL_OK_V1)
                    fail("not a notation in normal form", line);
            for (size_t index = 0u; index < 2u; index++) {
                fields[index].position = 0u;
                Atom *constant = read_core_constant(&arena, &fields[index]);
                if (!constant ||
                    !cetta_prime_regular_kernel_term_is_universe_sort_v1(
                        atom_expr2(
                            &arena, atom_symbol(&arena, "Sort"), constant)))
                    fail("the kernel declines a normal form", line);
                if (!universe_reads_back(&arena, constant))
                    fail("a printed universe does not read back", line);
            }
            int order = 0;
            bool equal = false;
            bool le = false;
            const CettaPrimeLevelV1 *successor = NULL;
            const CettaPrimeLevelV1 *maximum = NULL;
            const CettaPrimeLevelNotationV1 *under = NULL;
            const CettaPrimeLevelNotationV1 *read_maximum = NULL;
            CettaPrimeLevelViewV1 view = {0};
            fields[0].position = 0u;
            fields[1].position = 0u;
            Atom *written[2] = {
                written_level(
                    &arena, read_core_constant(&arena, &fields[0])),
                written_level(
                    &arena, read_core_constant(&arena, &fields[1])),
            };
            CettaPrimeRegularKernelBudget read_budget;
            cetta_prime_regular_kernel_budget_init(&read_budget, false, 0u);
            if (!written[0] || !written[1] ||
                cetta_prime_regular_term_closed_level_v1(
                    &arena,
                    atom_expr3(
                        &arena, atom_symbol(&arena, "max"), written[0],
                        written[1]),
                    &read_budget, &read_maximum).status !=
                    CETTA_PRIME_REGULAR_TERM_OK)
                fail("a maximum of two written levels does not read", line);
            complete =
                cetta_prime_level_notation_compare_v1(
                    notations[0], notations[1], &order) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_equal_v1(levels[0], levels[1], &equal) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_le_v1(levels[0], levels[1], &le) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_successor_v1(
                    &arena, levels[0], &successor) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_maximum_v1(
                    &arena, levels[0], levels[1], &maximum) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_notation_under_successor_v1(
                    &arena, notations[0], &under) == CETTA_PRIME_LEVEL_OK_V1;
            if (!complete) break;
            printf(
                "N cmp=%s eq=%d le=%d succ=",
                order < 0 ? "lt" : order > 0 ? "gt" : "eq", equal ? 1 : 0,
                le ? 1 : 0);
            cetta_prime_level_view_v1(successor, &view);
            print_notation(&arena, view.constant, line);
            printf(" max=");
            cetta_prime_level_view_v1(maximum, &view);
            print_notation(&arena, view.constant, line);
            printf(" under=");
            print_notation(&arena, under, line);
            printf(" read-max=");
            print_notation(&arena, read_maximum, line);
            printf("\n");
            break;
        }
        case 'U': {
            const CettaPrimeLevelNotationV1 *notations[2] = {NULL, NULL};
            const CettaPrimeLevelV1 *levels[2] = {NULL, NULL};
            Atom *universes[2] = {NULL, NULL};
            if (count != 2u) fail("a pair of levels has two fields", line);
            for (size_t index = 0u; index < 2u; index++) {
                if (!read_extended(
                        &arena, &fields[index], &notations[index]) ||
                    fields[index].position != fields[index].count ||
                    cetta_prime_level_constant_v1(
                        &arena, notations[index], &levels[index]) !=
                        CETTA_PRIME_LEVEL_OK_V1)
                    fail("not a level", line);
                fields[index].position = 0u;
                Atom *constant = read_core_extended(&arena, &fields[index]);
                universes[index] = constant
                    ? atom_expr2(
                          &arena, atom_symbol(&arena, "Sort"), constant)
                    : NULL;
                if (!universes[index] ||
                    !cetta_prime_regular_kernel_term_is_universe_sort_v1(
                        universes[index]))
                    fail("the kernel declines a level", line);
            }
            int order = 0;
            bool equal = false;
            bool le = false;
            const CettaPrimeLevelV1 *successor = NULL;
            const CettaPrimeLevelV1 *maximum = NULL;
            const CettaPrimeLevelNotationV1 *under = NULL;
            CettaPrimeLevelViewV1 view = {0};
            CettaPrimeRegularKernelBudget member_budget;
            cetta_prime_regular_kernel_budget_init(&member_budget, false, 0u);
            CettaPrimeRegularKernelResult member =
                cetta_prime_regular_kernel_check_intrinsic(
                    &arena, atom_symbol(&arena, "PrimeCtxNil"), universes[0],
                    universes[1], &member_budget);
            if (member.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
                member.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED)
                fail("the kernel gave no verdict on a membership", line);
            complete =
                cetta_prime_level_notation_compare_v1(
                    notations[0], notations[1], &order) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_equal_v1(levels[0], levels[1], &equal) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_le_v1(levels[0], levels[1], &le) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_successor_v1(
                    &arena, levels[0], &successor) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_maximum_v1(
                    &arena, levels[0], levels[1], &maximum) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_notation_under_successor_v1(
                    &arena, notations[0], &under) == CETTA_PRIME_LEVEL_OK_V1;
            if (!complete) break;
            printf(
                "U cmp=%s eq=%d le=%d succ=",
                order < 0 ? "lt" : order > 0 ? "gt" : "eq", equal ? 1 : 0,
                le ? 1 : 0);
            cetta_prime_level_view_v1(successor, &view);
            print_extended(&arena, view.constant, line);
            printf(" max=");
            cetta_prime_level_view_v1(maximum, &view);
            print_extended(&arena, view.constant, line);
            printf(" under=");
            print_extended(&arena, under, line);
            printf(
                " member=%d\n",
                member.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                    ? 1 : 0);
            break;
        }
        case 'W': {
            const CettaPrimeLevelV1 *levels[2] = {NULL, NULL};
            const CettaPrimeLevelNotationV1 *values[2] = {NULL, NULL};
            const CettaPrimeLevelV1 *successor = NULL;
            const CettaPrimeLevelV1 *maximum = NULL;
            Atom *core[2] = {NULL, NULL};
            Valuation valuation = {{NULL, NULL, NULL}};
            bool normal = true;
            bool equal = false;
            bool le = false;
            bool ge = false;
            if (count != 5u) fail("a written pair has five fields", line);
            for (size_t index = 0u; index < 2u; index++) {
                if (!read_level(&arena, &fields[index], &levels[index]) ||
                    fields[index].position != fields[index].count)
                    fail("not a level expression", line);
                fields[index].position = 0u;
                core[index] = read_core_level(&arena, &fields[index]);
                if (!core[index]) fail("not a level expression", line);
            }
            for (size_t index = 0u; index < 3u; index++)
                if (!read_notation(
                        &arena, &fields[index + 2u],
                        &valuation.values[index], &normal) ||
                    !normal ||
                    fields[index + 2u].position != fields[index + 2u].count)
                    fail("not a notation in normal form", line);
            CettaPrimeRegularKernelBudget member_budget;
            cetta_prime_regular_kernel_budget_init(&member_budget, false, 0u);
            CettaPrimeRegularKernelResult member =
                cetta_prime_regular_kernel_check_intrinsic(
                    &arena, atom_symbol(&arena, "PrimeCtxNil"),
                    atom_expr2(&arena, atom_symbol(&arena, "Sort"), core[0]),
                    atom_expr2(
                        &arena, atom_symbol(&arena, "Sort"),
                        atom_expr2(
                            &arena, atom_symbol(&arena, "LevelSucc"),
                            core[1])),
                    &member_budget);
            if (member.status != CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED &&
                member.status != CETTA_PRIME_REGULAR_KERNEL_REFUTED)
                fail("the kernel gave no verdict on a membership", line);
            complete =
                cetta_prime_level_equal_v1(levels[0], levels[1], &equal) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_le_v1(levels[0], levels[1], &le) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_le_v1(levels[1], levels[0], &ge) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_successor_v1(
                    &arena, levels[0], &successor) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_maximum_v1(
                    &arena, levels[0], levels[1], &maximum) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_evaluate_v1(
                    &arena, levels[0], valuate, &valuation, &values[0]) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_evaluate_v1(
                    &arena, levels[1], valuate, &valuation, &values[1]) ==
                    CETTA_PRIME_LEVEL_OK_V1;
            if (!complete) break;
            printf("W a=");
            print_written_level(&arena, levels[0], line);
            printf(" b=");
            print_written_level(&arena, levels[1], line);
            printf(
                " eq=%d le=%d ge=%d succ=", equal ? 1 : 0, le ? 1 : 0,
                ge ? 1 : 0);
            print_written_level(&arena, successor, line);
            printf(" max=");
            print_written_level(&arena, maximum, line);
            printf(" at=");
            print_extended(&arena, values[0], line);
            printf(" | ");
            print_extended(&arena, values[1], line);
            printf(
                " member=%d\n",
                member.status == CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED
                    ? 1 : 0);
            break;
        }
        case 'A': {
            const CettaPrimeLevelNotationV1 *notation = NULL;
            if (count != 1u) fail("an arithmetic case has one field", line);
            Atom *level = read_arithmetic(&arena, &fields[0]);
            if (!level || fields[0].position != fields[0].count)
                fail("not an arithmetic expression", line);
            CettaPrimeRegularKernelBudget read_budget;
            cetta_prime_regular_kernel_budget_init(&read_budget, false, 0u);
            CettaPrimeRegularTermElaborationV1 read =
                cetta_prime_regular_term_closed_level_v1(
                    &arena, level, &read_budget, &notation);
            if (read.status != CETTA_PRIME_REGULAR_TERM_OK)
                fail("an arithmetic expression does not read", line);
            printf("A ");
            print_notation(&arena, notation, line);
            printf("\n");
            break;
        }
        case 'T': {
            const CettaPrimeLevelNotationV1 *notation = NULL;
            bool normal = true;
            if (count != 1u) fail("a tree has one field", line);
            complete = read_notation(&arena, &fields[0], &notation, &normal);
            if (complete && fields[0].position != fields[0].count)
                fail("a tree is one code", line);
            if (!complete) break;
            fields[0].position = 0u;
            Atom *constant = read_core_constant(&arena, &fields[0]);
            if (!constant) fail("a tree is one code", line);
            printf(
                "T nf=%d core=%d\n", normal ? 1 : 0,
                cetta_prime_regular_kernel_term_is_universe_sort_v1(
                    atom_expr2(
                        &arena, atom_symbol(&arena, "Sort"), constant))
                    ? 1 : 0);
            break;
        }
        case 'E': {
            const CettaPrimeLevelV1 *levels[2] = {NULL, NULL};
            const CettaPrimeLevelV1 *successor = NULL;
            const CettaPrimeLevelV1 *maximum = NULL;
            bool equal = false;
            bool le = false;
            bool ge = false;
            if (count != 2u) fail("an expression pair has two fields", line);
            complete =
                read_level(&arena, &fields[0], &levels[0]) &&
                read_level(&arena, &fields[1], &levels[1]) &&
                cetta_prime_level_equal_v1(levels[0], levels[1], &equal) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_le_v1(levels[0], levels[1], &le) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_le_v1(levels[1], levels[0], &ge) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_successor_v1(
                    &arena, levels[0], &successor) ==
                    CETTA_PRIME_LEVEL_OK_V1 &&
                cetta_prime_level_maximum_v1(
                    &arena, levels[0], levels[1], &maximum) ==
                    CETTA_PRIME_LEVEL_OK_V1;
            if (!complete) break;
            printf("E a=");
            print_level(&arena, levels[0], line);
            printf(" b=");
            print_level(&arena, levels[1], line);
            printf(
                " eq=%d le=%d ge=%d succ=", equal ? 1 : 0, le ? 1 : 0,
                ge ? 1 : 0);
            print_level(&arena, successor, line);
            printf(" max=");
            print_level(&arena, maximum, line);
            printf("\n");
            break;
        }
        case 'S': {
            const CettaPrimeLevelV1 *source = NULL;
            const CettaPrimeLevelV1 *result = NULL;
            Substitution substitution = {{NULL, NULL, NULL}};
            if (count != 4u) fail("a substitution has four fields", line);
            complete = read_level(&arena, &fields[0], &source);
            for (size_t index = 0u; complete && index < 3u; index++)
                complete = read_level(
                    &arena, &fields[index + 1u], &substitution.levels[index]);
            complete = complete &&
                cetta_prime_level_substitute_v1(
                    &arena, source, substitute, &substitution, &result) ==
                    CETTA_PRIME_LEVEL_OK_V1;
            if (!complete) break;
            printf("S ");
            print_level(&arena, result, line);
            printf("\n");
            break;
        }
        case 'V': {
            const CettaPrimeLevelV1 *source = NULL;
            const CettaPrimeLevelNotationV1 *value = NULL;
            Valuation valuation = {{NULL, NULL, NULL}};
            bool normal = true;
            if (count != 4u) fail("an evaluation has four fields", line);
            complete = read_level(&arena, &fields[0], &source);
            for (size_t index = 0u; complete && index < 3u; index++)
                complete = read_notation(
                    &arena, &fields[index + 1u], &valuation.values[index],
                    &normal) && normal;
            complete = complete &&
                cetta_prime_level_evaluate_v1(
                    &arena, source, valuate, &valuation, &value) ==
                    CETTA_PRIME_LEVEL_OK_V1;
            if (!complete) break;
            printf("V ");
            print_notation(&arena, value, line);
            printf("\n");
            break;
        }
        case 'R': {
            if (count != 5u) fail("a raise has five fields", line);
            Atom *level = read_core_level(&arena, &fields[0]);
            Atom *bound = read_core_constant(&arena, &fields[1]);
            uint64_t parameters[3] = {0u, 1u, 2u};
            Atom *held[3] = {NULL, NULL, NULL};
            Atom *zero = atom_expr2(
                &arena, atom_symbol(&arena, "LevelConst"),
                atom_int(&arena, 0));
            complete = level && bound &&
                fields[0].position == fields[0].count &&
                fields[1].position == fields[1].count;
            for (size_t index = 0u; complete && index < 3u; index++) {
                held[index] = read_core_constant(&arena, &fields[index + 2u]);
                complete = held[index] &&
                    fields[index + 2u].position == fields[index + 2u].count;
                /* A parameter that holds zero holds nothing yet. */
                if (complete && atom_eq(held[index], zero)) held[index] = NULL;
            }
            if (!complete) break;
            CettaPrimeRegularKernelBudget raise_budget;
            cetta_prime_regular_kernel_budget_init(&raise_budget, false, 0u);
            bool outside = false;
            if (cetta_prime_regular_kernel_raise_level_parameters_v1(
                    &arena, level, bound, parameters, held, 3u,
                    &raise_budget, &outside) !=
                CETTA_PRIME_REGULAR_KERNEL_ESTABLISHED)
                fail("the kernel gave no verdict on a raise", line);
            if (outside) {
                printf("R outside\n");
                break;
            }
            printf("R ");
            for (size_t index = 0u; index < 3u; index++) {
                if (index != 0u) printf(" | ");
                if (!print_core_constant(held[index]))
                    fail("a raised parameter holds no closed level", line);
            }
            printf("\n");
            break;
        }
        default:
            fail("unknown case letter", line);
        }
        /* Every case has a verdict: a case without one is an error of the
         * cases or of the library, never a verdict to compare. */
        if (!complete) fail("a case has no verdict", line);
        for (size_t index = 0u; index < count; index++)
            free(fields[index].numbers);
        text = next;
    }
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    arena_free(&arena);
    free(contents);
    return 0;
}
