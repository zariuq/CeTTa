#define _POSIX_C_SOURCE 200809L
#include "run_observation.h"
#include "host_services.h"
#include "var_index.h"

#include <inttypes.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct RunObservation {
    FILE *file;
    CettaHostScope scope;
    CettaVarIndex variable_index;
    VarId *variables;
    size_t variable_count, variable_capacity;
    uint64_t query;
    pthread_mutex_t mutex;
    bool scope_entered, failed, values_complete, foreign_seen;
};

static const char *boolean(bool value) { return value ? "true" : "false"; }

static bool hex_bytes(FILE *file, const void *data, size_t length) {
    static const char digits[] = "0123456789abcdef";
    const unsigned char *bytes = data;
    if (fputc('"', file) == EOF) return false;
    for (size_t i = 0; i < length; i++) {
        if (fputc(digits[bytes[i] >> 4], file) == EOF ||
            fputc(digits[bytes[i] & 15], file) == EOF) return false;
    }
    return fputc('"', file) != EOF;
}

static bool effect_write(void *context, unsigned channel,
                          const char *bytes, size_t length) {
    RunObservation *o = context;
    pthread_mutex_lock(&o->mutex);
    FILE *destination = channel ? stderr : stdout;
    bool ok = fwrite(bytes, 1, length, destination) == length;
    ok = fprintf(o->file, "{\"event\":\"effect\",\"query\":%"PRIu64
                 ",\"channel\":%u,\"bytes\":", o->query, channel) >= 0 && ok;
    ok = hex_bytes(o->file, bytes, length) && ok;
    ok = fputs("}\n", o->file) != EOF && ok;
    o->failed |= !ok;
    pthread_mutex_unlock(&o->mutex);
    return ok;
}

static bool effect_flush(void *context, unsigned channel) {
    RunObservation *o = context;
    pthread_mutex_lock(&o->mutex);
    bool ok = fflush(channel ? stderr : stdout) == 0;
    o->failed |= !ok;
    pthread_mutex_unlock(&o->mutex);
    return ok;
}

static void foreign_activity(void *context, bool entering) {
    RunObservation *o = context;
    pthread_mutex_lock(&o->mutex);
    o->foreign_seen = true;
    if (fprintf(o->file, "{\"event\":\"foreign\",\"query\":%"PRIu64
                ",\"entering\":%s}\n", o->query, boolean(entering)) < 0)
        o->failed = true;
    pthread_mutex_unlock(&o->mutex);
}

RunObservation *run_observation_open(const char *path) {
    RunObservation *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->values_complete = true;
    cetta_var_index_init(&o->variable_index);
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (descriptor >= 0 && descriptor < 3) {
        int promoted = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
        close(descriptor);
        descriptor = promoted;
    }
    if (descriptor >= 0) {
        o->file = fdopen(descriptor, "w");
        if (!o->file) close(descriptor);
    }
    if (!o->file || pthread_mutex_init(&o->mutex, NULL)) {
        if (o->file) fclose(o->file);
        free(o);
        return NULL;
    }
    if (fputs("{\"event\":\"header\",\"version\":1,"
              "\"answers\":\"query-frontier\",\"effects\":\"thread-host-services\","
              "\"values\":\"cetta-structural-v1\"}\n", o->file) == EOF) {
        run_observation_free(o);
        return NULL;
    }
    CettaHostServices services = {.context = o, .write = effect_write,
        .flush = effect_flush, .foreign = foreign_activity};
    cetta_host_scope_enter(&o->scope, &services);
    o->scope_entered = true;
    return o;
}

void run_observation_free(RunObservation *o) {
    if (!o) return;
    if (o->scope_entered) cetta_host_scope_leave(&o->scope);
    if (o->file) fclose(o->file);
    cetta_var_index_free(&o->variable_index);
    free(o->variables);
    pthread_mutex_destroy(&o->mutex);
    free(o);
}

bool run_observation_query_begin(RunObservation *o, uint64_t query) {
    if (!o) return true;
    pthread_mutex_lock(&o->mutex);
    o->failed |= o->query != 0 || query == 0;
    o->query = query;
    if (fprintf(o->file, "{\"event\":\"begin\",\"query\":%"PRIu64"}\n", query) < 0)
        o->failed = true;
    bool ok = !o->failed;
    pthread_mutex_unlock(&o->mutex);
    return ok;
}

/* A preorder stream with explicit arities needs neither recursive printing
 * nor recursive JSON decoding. Shared variables are numbered across the
 * document, retaining aliasing within and between query frontiers. */
static bool value_tokens(RunObservation *o, const Atom *root, bool *complete) {
    typedef struct { const Atom *atom; size_t next; } Frame;
    Frame *stack = NULL;
    size_t length = 0, capacity = 0;
    const Atom *atom = root;
    bool comma = false, ok = true;
    for (;;) {
        if (comma && fputc(',', o->file) == EOF) { ok = false; break; }
        comma = true;
        if (!atom) { ok = false; break; }
        switch (atom->kind) {
        case ATOM_SYMBOL:
            ok = fputs("[\"symbol\",", o->file) != EOF &&
                hex_bytes(o->file, symbol_bytes(g_symbols, atom->sym_id),
                          symbol_len(g_symbols, atom->sym_id)) && fputc(']', o->file) != EOF;
            break;
        case ATOM_VAR: {
            if (var_id_is_none(atom->var_id)) {
                *complete = false;
                ok = fputs("[\"unobserved-variable\"]", o->file) != EOF;
                break;
            }
            size_t slot = cetta_var_index_find_records(&o->variable_index,
                o->variables, sizeof(*o->variables), o->variable_count, atom->var_id);
            if (slot == SIZE_MAX) {
                if (o->variable_count == o->variable_capacity) {
                    size_t grown = o->variable_capacity ? o->variable_capacity * 2 : 32;
                    if (grown < o->variable_capacity || grown > SIZE_MAX / sizeof(*o->variables)) { ok = false; break; }
                    VarId *next = realloc(o->variables, grown * sizeof(*o->variables));
                    if (!next) { ok = false; break; }
                    o->variables = next; o->variable_capacity = grown;
                }
                slot = o->variable_count++;
                o->variables[slot] = atom->var_id;
                if (!cetta_var_index_note_records(&o->variable_index, o->variables,
                        sizeof(*o->variables), o->variable_count)) { ok = false; break; }
            }
            ok = fprintf(o->file, "[\"variable\",%zu]", slot) >= 0;
            break;
        }
        case ATOM_EXPR:
            ok = fprintf(o->file, "[\"expression\",%zu]", (size_t)atom->expr.len) >= 0;
            if (ok && atom->expr.len) {
                if (length == capacity) {
                    size_t grown = capacity ? capacity * 2 : 32;
                    if (grown < capacity || grown > SIZE_MAX / sizeof(*stack)) { ok = false; break; }
                    Frame *next = realloc(stack, grown * sizeof(*stack));
                    if (!next) { ok = false; break; }
                    stack = next; capacity = grown;
                }
                stack[length++] = (Frame){atom, 1};
                atom = atom->expr.elems[0];
                continue;
            }
            break;
        case ATOM_GROUNDED:
            switch (atom->ground.gkind) {
            case GV_INT:
                ok = fprintf(o->file, "[\"integer\",\"%"PRId64"\"]", atom->ground.ival) >= 0;
                break;
            case GV_FLOAT: {
                uint64_t bits;
                _Static_assert(sizeof(bits) == sizeof(atom->ground.fval), "f64 observation");
                memcpy(&bits, &atom->ground.fval, sizeof(bits));
                ok = fprintf(o->file, "[\"float64\",\"%016"PRIx64"\"]", bits) >= 0;
                break;
            }
            case GV_BOOL:
                ok = fprintf(o->file, "[\"boolean\",%s]", boolean(atom->ground.bval)) >= 0;
                break;
            case GV_STRING:
                ok = fputs("[\"string\",", o->file) != EOF &&
                    hex_bytes(o->file, atom->ground.sval, atom->ground.slen) && fputc(']', o->file) != EOF;
                break;
            case GV_BIGINT:
            case GV_RATIONAL: {
                const char *text = atom->ground.gkind == GV_BIGINT
                    ? atom_bigint_cstr(atom) : atom_rational_cstr(atom);
                ok = text && fprintf(o->file, "[\"%s\",", atom->ground.gkind == GV_BIGINT
                        ? "big-integer" : "rational") >= 0 &&
                    hex_bytes(o->file, text, strlen(text)) && fputc(']', o->file) != EOF;
                break;
            }
            case GV_INTERNAL_TAG:
                ok = fprintf(o->file, "[\"internal-tag\",%"PRId64"]", atom->ground.ival) >= 0;
                break;
            default:
                *complete = false;
                ok = fprintf(o->file, "[\"unobserved-grounded\",%u]", (unsigned)atom->ground.gkind) >= 0;
                break;
            }
            break;
        default:
            ok = false;
            break;
        }
        if (!ok) break;
        while (length && stack[length - 1].next == stack[length - 1].atom->expr.len) length--;
        if (!length) break;
        Frame *frame = &stack[length - 1];
        atom = frame->atom->expr.elems[frame->next++];
    }
    free(stack);
    return ok;
}

void run_observation_answers(RunObservation *o, Atom *const *answers, size_t count) {
    if (!o) return;
    pthread_mutex_lock(&o->mutex);
    o->failed |= o->query == 0;
    for (size_t i = 0; !o->failed && i < count; i++) {
        bool complete = true;
        bool ok = fprintf(o->file, "{\"event\":\"answer\",\"query\":%"PRIu64",\"value\":[", o->query) >= 0;
        ok = ok && value_tokens(o, answers[i], &complete);
        ok = ok && fprintf(o->file, "],\"complete\":%s}\n", boolean(complete)) >= 0;
        o->values_complete &= complete;
        o->failed |= !ok;
    }
    pthread_mutex_unlock(&o->mutex);
}

void run_observation_query_end(RunObservation *o, RunReportStopKind stop,
                               bool output_ok, bool cleanup_ok) {
    if (!o) return;
    pthread_mutex_lock(&o->mutex);
    if (fprintf(o->file, "{\"event\":\"end\",\"query\":%"PRIu64",\"stop\":%u,"
                "\"output_ok\":%s,\"cleanup_ok\":%s}\n", o->query, (unsigned)stop,
                boolean(output_ok), boolean(cleanup_ok)) < 0) o->failed = true;
    o->failed |= o->query == 0;
    o->query = 0;
    pthread_mutex_unlock(&o->mutex);
}

bool run_observation_finish(RunObservation *o, const RunReportDocStatus *status,
                            int dialect_exit) {
    if (!o) return true;
    pthread_mutex_lock(&o->mutex);
    bool ok = !o->failed && !o->query;
    ok = fprintf(o->file, "{\"event\":\"final\",\"dialect_exit\":%d,\"contract_exit\":%u,"
        "\"queries_declared\":%"PRIu64",\"queries_unsettled\":%"PRIu64","
        "\"values_complete\":%s,\"foreign_activity\":%s,\"framing_ok\":%s}\n",
        dialect_exit, status->exit_code, status->queries_declared, status->queries_unsettled,
        boolean(o->values_complete), boolean(o->foreign_seen), boolean(ok)) >= 0 && ok;
    if (fflush(o->file) || ferror(o->file)) ok = false;
    if (fclose(o->file)) ok = false;
    o->file = NULL;
    pthread_mutex_unlock(&o->mutex);
    return ok;
}
