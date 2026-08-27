#include "radix_digit_c_emitter_v1.h"

#include <stdarg.h>

static bool emitf(FILE *output, const char *format, ...) {
    va_list arguments;
    int written;
    va_start(arguments, format);
    written = vfprintf(output, format, arguments);
    va_end(arguments);
    return written >= 0;
}

static bool emit_values(
    FILE *output,
    const uint32_t *values,
    uint32_t len) {
    uint32_t index;
    for (index = 0u; index < len; ++index)
        if (!emitf(output, "%s%uu", index == 0u ? "" : ", ", values[index]))
            return false;
    return true;
}

static bool emit_prelude(FILE *output) {
    return emitf(output,
        "#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n"
        "#include <string.h>\n\n"
        "typedef struct { uint32_t input_len, inputs[4], output_len, outputs[2]; "
        "uint32_t source_rule_len, source_rule_indices[32]; } GeneratedRow;\n"
        "typedef struct { uint32_t row_len; const GeneratedRow *rows; } GeneratedTable;\n\n"
        "typedef struct { uint32_t kind, pc, table_index, row_index; "
        "uint32_t source_rule_len, source_rule_indices[32]; } GeneratedEvent;\n\n"
        "static int append_event(GeneratedEvent **events, uint32_t *event_len,\n"
        "    uint32_t *event_capacity, const GeneratedEvent *event) {\n"
        "  if (*event_len == *event_capacity) {\n"
        "    uint32_t next_capacity = *event_capacity == 0u ? 16u : *event_capacity * 2u;\n"
        "    GeneratedEvent *next;\n"
        "    if (next_capacity < *event_capacity) return 0;\n"
        "    next = realloc(*events, (size_t)next_capacity * sizeof(**events));\n"
        "    if (!next) return 0;\n"
        "    *events = next; *event_capacity = next_capacity;\n"
        "  }\n"
        "  (*events)[(*event_len)++] = *event; return 1;\n"
        "}\n\n"
        "static const GeneratedRow *find_row(const GeneratedTable *table,\n"
        "    const uint32_t *inputs, uint32_t input_len, uint32_t *row_index) {\n"
        "  uint32_t row, item;\n"
        "  for (row = 0u; row < table->row_len; ++row) {\n"
        "    if (table->rows[row].input_len != input_len) continue;\n"
        "    for (item = 0u; item < input_len; ++item)\n"
        "      if (table->rows[row].inputs[item] != inputs[item]) break;\n"
        "    if (item == input_len) { *row_index = row; return &table->rows[row]; }\n"
        "  }\n"
        "  return NULL;\n"
        "}\n\n");
}

static bool emit_table(
    FILE *output,
    const CettaRadixDigitV1Table *table,
    uint32_t table_index) {
    uint32_t row_index;
    if (!emitf(output,
            "static const GeneratedRow generated_table_%u_rows[] = {\n",
            table_index))
        return false;
    for (row_index = 0u; row_index < table->row_len; ++row_index) {
        const CettaRadixDigitV1TableRow *row = &table->rows[row_index];
        if (row->input_len > 4u || row->output_len > 2u ||
                row->source_rule_len > 32u ||
                !emitf(output, "  {.input_len=%uu, .inputs={", row->input_len) ||
                !emit_values(output, row->inputs, row->input_len) ||
                !emitf(output, "}, .output_len=%uu, .outputs={", row->output_len) ||
                !emit_values(output, row->outputs, row->output_len) ||
                !emitf(output, "}, .source_rule_len=%uu, .source_rule_indices={",
                    row->source_rule_len) ||
                !emit_values(output, row->source_rule_indices,
                    row->source_rule_len) ||
                !emitf(output, "}},\n"))
            return false;
    }
    return emitf(output,
        "};\nstatic const GeneratedTable generated_table_%u = "
        "{.row_len=%uu, .rows=generated_table_%u_rows};\n\n",
        table_index, table->row_len, table_index);
}

static bool emit_fuel_guard(FILE *output, uint32_t pc) {
    return emitf(output,
        "  if (fuel == 0u) goto resource_fault;\n"
        "  --fuel; ++steps;\n"
        "  { const GeneratedEvent event = {.kind=0u, .pc=%uu};\n"
        "    if (!append_event(&events, &event_len, &event_capacity, &event))\n"
        "      goto resource_fault; }\n", pc);
}

static bool emit_instruction(
    FILE *output,
    uint32_t pc,
    uint32_t radix,
    uint32_t table_len,
    CettaRadixDigitV1InstructionKind kind,
    const CettaRadixDigitV1Instruction *instruction) {
    const uint32_t *a = instruction->arguments;
    uint32_t cursor;
    uint32_t input_len;
    uint32_t output_len;
    uint32_t index;
    if (!emitf(output, "radix_digit_pc_%u:\n", pc) || !emit_fuel_guard(output, pc))
        return false;
    switch (kind) {
    case CETTA_RADIX_DIGIT_V1_SET:
        return emitf(output, "  registers[%uu] = %uu; goto radix_digit_pc_%u;\n",
            a[0], a[1], a[2]);
    case CETTA_RADIX_DIGIT_V1_COPY:
        return emitf(output,
            "  registers[%uu] = registers[%uu]; goto radix_digit_pc_%u;\n",
            a[1], a[0], a[2]);
    case CETTA_RADIX_DIGIT_V1_INCREMENT:
        return emitf(output,
            "  if (registers[%uu] == UINT32_MAX) goto resource_fault;\n"
            "  ++registers[%uu]; goto radix_digit_pc_%u;\n", a[0], a[0], a[1]);
    case CETTA_RADIX_DIGIT_V1_LENGTH:
        if (a[0] == 0u)
            return emitf(output,
                "  registers[%uu] = first_len; goto radix_digit_pc_%u;\n", a[1], a[2]);
        if (a[0] == 1u)
            return emitf(output,
                "  registers[%uu] = second_len; goto radix_digit_pc_%u;\n", a[1], a[2]);
        return emitf(output,
            "  registers[%uu] = output_len; goto radix_digit_pc_%u;\n", a[1], a[2]);
    case CETTA_RADIX_DIGIT_V1_READ_OR_ZERO:
        if (a[0] == 0u)
            return emitf(output,
                "  registers[%uu] = registers[%uu] < first_len ? "
                "first[registers[%uu]] : 0u;\n"
                "  if (registers[%uu] >= %uu) goto language_fault;\n"
                "  goto radix_digit_pc_%u;\n",
                a[2], a[1], a[1], a[2], radix, a[3]);
        if (a[0] == 1u)
            return emitf(output,
                "  registers[%uu] = registers[%uu] < second_len ? "
                "second[registers[%uu]] : 0u;\n"
                "  if (registers[%uu] >= %uu) goto language_fault;\n"
                "  goto radix_digit_pc_%u;\n",
                a[2], a[1], a[1], a[2], radix, a[3]);
        return emitf(output,
            "  registers[%uu] = registers[%uu] < output_len ? "
            "output[registers[%uu]] : 0u;\n"
            "  if (registers[%uu] >= %uu) goto language_fault;\n"
            "  goto radix_digit_pc_%u;\n",
            a[2], a[1], a[1], a[2], radix, a[3]);
    case CETTA_RADIX_DIGIT_V1_WRITE:
        if (a[0] != 2u)
            return false;
        return emitf(output,
            "  if (registers[%uu] >= %uu) goto language_fault;\n"
            "  if (registers[%uu] > output_len) goto language_fault;\n"
            "  if (registers[%uu] == output_len) {\n"
            "    if (output_len >= output_limit) goto resource_fault;\n"
            "    if (output_len == output_capacity) {\n"
            "      uint32_t next_capacity = output_capacity == 0u ? 8u : output_capacity * 2u;\n"
            "      uint32_t *next_output;\n"
            "      if (next_capacity < output_capacity || next_capacity > output_limit)\n"
            "        next_capacity = output_limit;\n"
            "      if (next_capacity <= output_capacity) goto resource_fault;\n"
            "      next_output = realloc(output, (size_t)next_capacity * sizeof(*output));\n"
            "      if (!next_output) goto resource_fault;\n"
            "      output = next_output; output_capacity = next_capacity;\n"
            "    }\n"
            "    output[output_len++] = registers[%uu];\n"
            "  } else output[registers[%uu]] = registers[%uu];\n"
            "  goto radix_digit_pc_%u;\n",
            a[2], radix, a[1], a[1], a[2], a[1], a[2], a[3]);
    case CETTA_RADIX_DIGIT_V1_LOOKUP:
        if (instruction->table_index >= table_len)
            return false;
        cursor = 0u;
        input_len = a[cursor++];
        if (!emitf(output, "  { const uint32_t inputs[] = {"))
            return false;
        for (index = 0u; index < input_len; ++index)
            if (!emitf(output, "%sregisters[%uu]",
                    index == 0u ? "" : ", ", a[cursor++]))
                return false;
        output_len = a[cursor++];
        if (!emitf(output,
                "}; uint32_t row_index; const GeneratedRow *row = "
                "find_row(&generated_table_%u, inputs, %uu, &row_index);\n"
                "  if (!row || row->output_len != %uu) "
                "goto language_fault;\n",
                instruction->table_index, input_len, output_len))
            return false;
        if (!emitf(output,
                "  { GeneratedEvent event = {.kind=1u, .pc=%uu, "
                ".table_index=%uu, .row_index=row_index, "
                ".source_rule_len=row->source_rule_len};\n"
                "    uint32_t source_index;\n"
                "    for (source_index = 0u; source_index < row->source_rule_len; "
                "++source_index)\n"
                "      event.source_rule_indices[source_index] = "
                "row->source_rule_indices[source_index];\n"
                "    if (!append_event(&events, &event_len, &event_capacity, "
                "&event)) goto resource_fault; }\n",
                pc, instruction->table_index))
            return false;
        for (index = 0u; index < output_len; ++index)
            if (!emitf(output, "  registers[%uu] = row->outputs[%uu];\n",
                    a[cursor++], index))
                return false;
        return emitf(output, "  goto radix_digit_pc_%u; }\n", a[cursor]);
    case CETTA_RADIX_DIGIT_V1_BRANCH_LT:
        return emitf(output,
            "  if (registers[%uu] < registers[%uu]) goto radix_digit_pc_%u; "
            "else goto radix_digit_pc_%u;\n", a[0], a[1], a[2], a[3]);
    case CETTA_RADIX_DIGIT_V1_BRANCH_EQ:
        return emitf(output,
            "  if (registers[%uu] == %uu) goto radix_digit_pc_%u; "
            "else goto radix_digit_pc_%u;\n", a[0], a[1], a[2], a[3]);
    case CETTA_RADIX_DIGIT_V1_JUMP:
        return emitf(output, "  goto radix_digit_pc_%u;\n", a[0]);
    case CETTA_RADIX_DIGIT_V1_RETURN_BUFFER:
        if (a[0] != 2u)
            return false;
        return emitf(output,
            "  *digits_out = output; *digit_len_out = output_len;\n"
            "  *events_out = events; *event_len_out = event_len; "
            "*steps_out = steps; return 1;\n");
    case CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT:
        break;
    }
    return false;
}

static bool emit_program(
    FILE *output,
    uint32_t radix,
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program) {
    uint32_t index;
    if (!emitf(output,
            "static int run_generated(const uint32_t *first, uint32_t first_len,\n"
            "    const uint32_t *second, uint32_t second_len, uint32_t output_limit,\n"
            "    uint32_t fuel, uint32_t **digits_out, uint32_t *digit_len_out,\n"
            "    GeneratedEvent **events_out, uint32_t *event_len_out,\n"
            "    uint32_t *steps_out) {\n"
            "  uint32_t registers[16] = {0};\n"
            "  uint32_t *output = NULL, output_len = 0u, output_capacity = 0u;\n"
            "  GeneratedEvent *events = NULL;\n"
            "  uint32_t event_len = 0u, event_capacity = 0u, steps = 0u;\n"
            "  goto radix_digit_pc_0;\n"))
        return false;
    for (index = 0u; index < program->instruction_len; ++index) {
        CettaRadixDigitV1InstructionKind kind;
        if (!cetta_radix_digit_v1_target_constructor(profile,
                program->instructions[index].constructor_term_index, &kind) ||
                !emit_instruction(output, index, radix, program->table_len, kind,
                    &program->instructions[index]))
            return false;
    }
    return emitf(output,
        "language_fault:\n  free(output); free(events); return -1;\n"
        "resource_fault:\n  free(output); free(events); return -3;\n"
        "}\n\n");
}

static bool emit_driver(FILE *output, uint32_t radix) {
    return emitf(output,
        "static int digit_value(unsigned char c) {\n"
        "  if (c >= '0' && c <= '9') return (int)(c - '0');\n"
        "  if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;\n"
        "  if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;\n"
        "  return -1;\n}\n\n"
        "static int read_digits(const char *text, uint32_t **out, uint32_t *len) {\n"
        "  size_t n = strlen(text), i; uint32_t *digits;\n"
        "  if (n == 1u && text[0] == '0') { *out = NULL; *len = 0u; return 1; }\n"
        "  if (n == 0u || text[0] == '0' || n > UINT32_MAX) return 0;\n"
        "  digits = malloc(n * sizeof(*digits)); if (!digits) return 0;\n"
        "  for (i = 0u; i < n; ++i) {\n"
        "    int d = digit_value((unsigned char)text[n - i - 1u]);\n"
        "    if (d < 0 || (uint32_t)d >= %uu) { free(digits); return 0; }\n"
        "    digits[i] = (uint32_t)d;\n  }\n"
        "  *out = digits; *len = (uint32_t)n; return 1;\n}\n\n"
        "static char digit_character(uint32_t digit) {\n"
        "  return digit < 10u ? (char)('0' + digit) : (char)('A' + digit - 10u);\n}\n\n"
        "static void print_receipt(const GeneratedEvent *events, uint32_t event_len,\n"
        "    uint32_t steps) {\n"
        "  uint32_t event_index, source_index;\n"
        "  printf(\"steps:%%u\\n\", steps);\n"
        "  for (event_index = 0u; event_index < event_len; ++event_index) {\n"
        "    const GeneratedEvent *event = &events[event_index];\n"
        "    if (event->kind == 0u) printf(\"event:execute:%%u\\n\", event->pc);\n"
        "    else {\n"
        "      printf(\"event:table-row:%%u:%%u:%%u:\", event->pc,\n"
        "        event->table_index, event->row_index);\n"
        "      for (source_index = 0u; source_index < event->source_rule_len;\n"
        "          ++source_index)\n"
        "        printf(\"%%s%%u\", source_index == 0u ? \"\" : \",\",\n"
        "          event->source_rule_indices[source_index]);\n"
        "      fputc('\\n', stdout);\n"
        "    }\n"
        "  }\n"
        "}\n\n"
        "int main(int argc, char **argv) {\n"
        "  uint32_t *first = NULL, *second = NULL, *digits = NULL;\n"
        "  GeneratedEvent *events = NULL;\n"
        "  uint32_t first_len = 0u, second_len = 0u, digit_len = 0u;\n"
        "  uint32_t event_len = 0u, steps = 0u, i; int status;\n"
        "  int show_receipt = argc == 4 && strcmp(argv[3], \"--receipt\") == 0;\n"
        "  if ((argc != 3 && !show_receipt) ||\n"
        "      !read_digits(argv[1], &first, &first_len) ||\n"
        "      !read_digits(argv[2], &second, &second_len)) {\n"
        "    fputs(\"invalid input\\n\", stderr); free(first); free(second); return 2;\n  }\n"
        "  status = run_generated(first, first_len, second, second_len,\n"
        "    first_len + second_len + 4u,\n"
        "    64u * (first_len + 1u) * (second_len + 1u), &digits, &digit_len,\n"
        "    &events, &event_len, &steps);\n"
        "  free(first); free(second);\n"
        "  if (status != 1) { fprintf(stderr, \"fault:%%d\\n\", status); return 3; }\n"
        "  fputs(\"value:\", stdout);\n"
        "  if (digit_len == 0u) fputc('0', stdout);\n"
        "  else for (i = digit_len; i > 0u; --i)\n"
        "    fputc(digit_character(digits[i - 1u]), stdout);\n"
        "  fputc('\\n', stdout);\n"
        "  if (show_receipt) print_receipt(events, event_len, steps);\n"
        "  free(events); free(digits); return 0;\n}\n",
        radix);
}

static bool program_uses_table(
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program,
    uint32_t table_index) {
    uint32_t index;
    for (index = 0u; index < program->instruction_len; ++index) {
        CettaRadixDigitV1InstructionKind kind;
        if (!cetta_radix_digit_v1_target_constructor(profile,
                program->instructions[index].constructor_term_index, &kind))
            return false;
        if (kind == CETTA_RADIX_DIGIT_V1_LOOKUP &&
                program->instructions[index].table_index == table_index)
            return true;
    }
    return false;
}

bool cetta_radix_digit_v1_emit_c(
    FILE *output,
    uint32_t radix,
    const CettaRadixDigitV1TargetProfile *profile,
    const CettaRadixDigitV1Program *program) {
    uint32_t index;
    if (!output || radix < 2u || radix > 16u ||
            !cetta_radix_digit_v1_program_valid(profile, program) || !emit_prelude(output))
        return false;
    for (index = 0u; index < program->table_len; ++index)
        if (program_uses_table(profile, program, index) &&
                !emit_table(output, &program->tables[index], index))
            return false;
    return emit_program(output, radix, profile, program) &&
        emit_driver(output, radix) && fflush(output) == 0 && !ferror(output);
}
