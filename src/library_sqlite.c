#include "library_sqlite.h"
#include "symbol.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if CETTA_BUILD_WITH_SQLITE
#include <sqlite3.h>

static const char *text(Atom *value) {
    if (value && value->kind == ATOM_SYMBOL) return atom_name_cstr(value);
    if (value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING &&
        !memchr(value->ground.sval, 0, value->ground.slen)) return value->ground.sval;
    return NULL;
}
static Atom *failure(Arena *a, int code, const char *message, bool commit) {
    int primary = code & 255;
    const char *kind = primary == SQLITE_BUSY || primary == SQLITE_LOCKED ? "busy" :
        primary == SQLITE_CONSTRAINT ? "constraint" :
        primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB ? "corrupt" :
        primary == SQLITE_IOERR || primary == SQLITE_CANTOPEN || primary == SQLITE_FULL ? "io" : "sql";
    const char *outcome = commit && (primary == SQLITE_IOERR || primary == SQLITE_FULL) ?
        "unknown-outcome" : "failed";
    return atom_expr(a, (Atom *[]){atom_symbol(a, "sqlite:error"), atom_symbol(a, kind),
        atom_int(a, code), atom_string(a, message ? message : "SQLite error"), atom_symbol(a, outcome)}, 5);
}
static bool timeout(Atom *value, int *milliseconds) {
    if (!value || value->kind != ATOM_GROUNDED) return false;
    int64_t n;
    if (value->ground.gkind == GV_INT) n = value->ground.ival;
    else if (value->ground.gkind == GV_FLOAT && isfinite(value->ground.fval) &&
        trunc(value->ground.fval) == value->ground.fval && value->ground.fval >= 0 && value->ground.fval <= INT_MAX)
        n = (int64_t)value->ground.fval;
    else return false;
    if (n < 0 || n > INT_MAX) return false;
    *milliseconds = (int)n; return true;
}
static int bind(sqlite3_stmt *statement, int index, Atom *value) {
    if (atom_is_symbol(value, "sqlite:null")) return sqlite3_bind_null(statement, index);
    if (value && value->kind == ATOM_SYMBOL) {
        const char *s = atom_name_cstr(value);
        return sqlite3_bind_text(statement, index, s, -1, SQLITE_TRANSIENT);
    }
    if (value && value->kind == ATOM_EXPR && value->expr.len == 2 &&
        atom_is_symbol(value->expr.elems[0], "sqlite:blob")) {
        Atom *bytes = value->expr.elems[1];
        if (bytes->kind == ATOM_GROUNDED && bytes->ground.gkind == GV_STRING)
            return sqlite3_bind_blob64(statement, index, bytes->ground.sval, bytes->ground.slen, SQLITE_TRANSIENT);
    }
    if (!value || value->kind != ATOM_GROUNDED) return SQLITE_MISMATCH;
    switch (value->ground.gkind) {
        case GV_INT: return sqlite3_bind_int64(statement, index, value->ground.ival);
        case GV_FLOAT: return sqlite3_bind_double(statement, index, value->ground.fval);
        case GV_BOOL: return sqlite3_bind_int(statement, index, value->ground.bval ? 1 : 0);
        case GV_STRING: return sqlite3_bind_text64(statement, index, value->ground.sval,
            value->ground.slen, SQLITE_TRANSIENT, SQLITE_UTF8);
        default: return SQLITE_MISMATCH;
    }
}
static Atom *column(Arena *a, sqlite3_stmt *statement, int index) {
    switch (sqlite3_column_type(statement, index)) {
        case SQLITE_INTEGER: return atom_int(a, sqlite3_column_int64(statement, index));
        case SQLITE_FLOAT: return atom_float(a, sqlite3_column_double(statement, index));
        case SQLITE_TEXT: {
            const unsigned char *s = sqlite3_column_text(statement, index);
            int n = sqlite3_column_bytes(statement, index);
            return atom_string_n(a, (const char *)(s ? s : (const unsigned char *)""), n);
        }
        case SQLITE_BLOB: {
            const void *s = sqlite3_column_blob(statement, index);
            int n = sqlite3_column_bytes(statement, index);
            return atom_expr2(a, atom_symbol(a, "sqlite:blob"), atom_string_n(a, s ? s : "", n));
        }
        default: return atom_symbol(a, "sqlite:null");
    }
}
/* An observation is refused ATTACH while it is prepared, so it opens no
 * other database file. */
static int observe_authorizer(void *unused, int action, const char *first,
                              const char *second, const char *database, const char *trigger) {
    (void)unused; (void)first; (void)second; (void)database; (void)trigger;
    return action == SQLITE_ATTACH ? SQLITE_DENY : SQLITE_OK;
}
static int statement_run(sqlite3 *db, Arena *a, Atom *source, Atom *parameters, bool observe,
                         Atom **answer) {
    const char *sql = text(source);
    if (!sql || !parameters || parameters->kind != ATOM_EXPR) {
        *answer = failure(a, SQLITE_MISMATCH, "expected SQL text and a parameter list", false);
        return SQLITE_MISMATCH;
    }
    sqlite3_stmt *statement = NULL, *extra = NULL; const char *tail = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &statement, &tail);
    if (rc == SQLITE_OK && statement) {
        /* Parameter binding belongs to one statement. A transaction accepts
         * any number of statement/parameter pairs without guessing policy. */
        const char *rest = tail;
        while (*rest && rc == SQLITE_OK && !extra) {
            const char *next;
            rc = sqlite3_prepare_v2(db, rest, -1, &extra, &next);
            if (next == rest) break;
            rest = next;
        }
        if (extra) {
            sqlite3_finalize(extra); sqlite3_finalize(statement);
            *answer = failure(a, SQLITE_MISUSE, observe ? "sqlite:observe runs one statement" :
                "use sqlite:transaction for multiple statements", false);
            return SQLITE_MISUSE;
        }
    }
    if (rc != SQLITE_OK || !statement) {
        *answer = failure(a, rc == SQLITE_OK ? SQLITE_MISUSE : rc,
            rc == SQLITE_OK ? "SQL contains no statement" :
            observe && (rc & 255) == SQLITE_AUTH ? "sqlite:observe does not attach databases" :
            sqlite3_errmsg(db), false);
        sqlite3_finalize(statement); return rc == SQLITE_OK ? SQLITE_MISUSE : rc;
    }
    /* SQLite reports whether the prepared program can write. The read-only
     * connection and query_only are further layers, not the check itself. */
    if (observe && !sqlite3_stmt_readonly(statement)) {
        sqlite3_finalize(statement);
        *answer = failure(a, SQLITE_READONLY, "sqlite:observe runs only statements that do not write", false);
        return SQLITE_READONLY;
    }
    if (parameters->expr.len != (uint32_t)sqlite3_bind_parameter_count(statement)) rc = SQLITE_RANGE;
    for (uint32_t i = 0; rc == SQLITE_OK && i < parameters->expr.len; ++i)
        rc = bind(statement, (int)i + 1, parameters->expr.elems[i]);
    int count = sqlite3_column_count(statement);
    Atom **names = count ? cetta_malloc((size_t)count * sizeof(*names)) : NULL;
    Atom **values = count ? cetta_malloc((size_t)count * sizeof(*values)) : NULL;
    for (int i = 0; i < count; ++i) names[i] = atom_string(a, sqlite3_column_name(statement, i));
    Atom **rows = NULL; size_t size = 0, capacity = 0;
    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
            if (size == capacity) {
                capacity = capacity ? capacity * 2 : 16;
                rows = cetta_realloc(rows, capacity * sizeof(*rows));
            }
            for (int i = 0; i < count; ++i) values[i] = column(a, statement, i);
            rows[size++] = atom_expr(a, values, (uint32_t)count);
        }
    }
    if (rc == SQLITE_DONE) {
        *answer = atom_expr(a, (Atom *[]){atom_symbol(a, "sqlite:result"),
            atom_expr(a, names, (uint32_t)count), atom_expr(a, rows, (uint32_t)size),
            atom_int(a, sqlite3_changes64(db)), atom_int(a, sqlite3_last_insert_rowid(db))}, 5);
        rc = SQLITE_OK;
    } else *answer = failure(a, sqlite3_extended_errcode(db) == SQLITE_OK ? rc : sqlite3_extended_errcode(db),
        rc == SQLITE_RANGE ? "parameter count does not match SQL" :
        rc == SQLITE_MISMATCH ? "unsupported bound value type" : sqlite3_errmsg(db), false);
    sqlite3_finalize(statement); free(names); free(values); free(rows);
    return rc;
}
static Atom *run(Arena *a, Atom **args, uint32_t nargs, bool transaction, bool observe) {
    int wait = 0;
    const char *path = nargs == 4 ? text(args[0]) : NULL;
    const char *mode = transaction && nargs == 4 ? text(args[1]) : NULL;
    Atom *statements = transaction && nargs == 4 ? args[2] : NULL;
    if (!path || !timeout(args[3], &wait) ||
        (transaction && (!mode || (strcmp(mode, "deferred") && strcmp(mode, "immediate")) ||
                         !statements || statements->kind != ATOM_EXPR)))
        return failure(a, SQLITE_MISUSE, "expected path, SQL/parameters or transaction mode/statements, and SQLite busy milliseconds", false);
    /* An observation opens an existing database read-only: it never creates
     * the file or writes a page of it, and leaves WAL checkpoints to writers.
     * Like any SQLite reader it still uses the -wal and -shm files. */
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(path, &db, (observe ? SQLITE_OPEN_READONLY :
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) {
        Atom *error = failure(a, rc, !db ? "cannot open database" :
            observe && sqlite3_system_errno(db) == ENOENT ? "database file does not exist" :
            sqlite3_errmsg(db), false);
        sqlite3_close(db); return error;
    }
    sqlite3_extended_result_codes(db, 1); sqlite3_busy_timeout(db, wait);
    if (!observe) rc = sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
    else {
        rc = sqlite3_db_config(db, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, (int *)NULL);
        if (rc == SQLITE_OK) rc = sqlite3_exec(db, "PRAGMA query_only=1", NULL, NULL, NULL);
        if (rc == SQLITE_OK) rc = sqlite3_set_authorizer(db, observe_authorizer, NULL);
    }
    Atom *answer = NULL;
    if (rc == SQLITE_OK && !transaction) rc = statement_run(db, a, args[1], args[2], observe, &answer);
    else if (rc == SQLITE_OK) {
        rc = sqlite3_exec(db, !strcmp(mode, "immediate") ? "BEGIN IMMEDIATE" : "BEGIN DEFERRED", NULL, NULL, NULL);
        Atom **results = statements->expr.len ? cetta_malloc(statements->expr.len * sizeof(*results)) : NULL;
        for (uint32_t i = 0; rc == SQLITE_OK && i < statements->expr.len; ++i) {
            Atom *entry = statements->expr.elems[i];
            if (!entry || entry->kind != ATOM_EXPR || entry->expr.len != 3 ||
                !atom_is_symbol(entry->expr.elems[0], "sqlite:statement")) {
                answer = failure(a, SQLITE_MISUSE, "expected (sqlite:statement SQL parameters)", false);
                rc = SQLITE_MISUSE;
            } else rc = statement_run(db, a, entry->expr.elems[1], entry->expr.elems[2], false, &results[i]);
            if (rc != SQLITE_OK && !answer) answer = results[i];
        }
        if (rc == SQLITE_OK) {
            rc = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
            answer = rc == SQLITE_OK ? atom_expr2(a, atom_symbol(a, "sqlite:committed"),
                atom_expr(a, results, statements->expr.len)) : failure(a, sqlite3_extended_errcode(db), sqlite3_errmsg(db), true);
        }
        free(results);
    }
    if (!answer) answer = failure(a, sqlite3_extended_errcode(db), sqlite3_errmsg(db), false);
    if (!sqlite3_get_autocommit(db)) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        if (!transaction && rc == SQLITE_OK)
            answer = failure(a, SQLITE_MISUSE, observe ? "unclosed transaction rolled back" :
                "unclosed transaction rolled back; use sqlite:transaction", false);
    }
    sqlite3_close(db);
    return answer;
}
#endif

Atom *cetta_sqlite_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    SymbolId id = atom_symbol_current_id(head);
    if (id != g_builtin_syms.lib_sqlite_query && id != g_builtin_syms.lib_sqlite_transaction &&
        id != g_builtin_syms.lib_sqlite_observe) return NULL;
#if CETTA_BUILD_WITH_SQLITE
    if (nargs != 4) return failure(arena, SQLITE_MISUSE, "expected four SQLite arguments", false);
    return run(arena, args, nargs, id == g_builtin_syms.lib_sqlite_transaction,
               id == g_builtin_syms.lib_sqlite_observe);
#else
    (void)arena; (void)args; (void)nargs;
    return NULL;
#endif
}
