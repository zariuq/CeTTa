#define _POSIX_C_SOURCE 200809L
#include "durable_store.h"

#include <sqlite3.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#if SQLITE_VERSION_NUMBER < 3037000
#error "Durable spaces require SQLite 3.37.0 or newer"
#endif

#define DURABLE_APPLICATION_ID 1129595986 /* CTDR */
#define DURABLE_SCHEMA 1
#define DURABLE_NAME_BYTES 255

struct CettaDurableStore {
    sqlite3 *db;
    int lock_fd;
    pthread_mutex_t mutex;
    CettaDurableLimits limits;
    bool poisoned;
    const void *runtime_owner;
};

#ifdef CETTA_DURABLE_TEST
extern void cetta_durable_test_boundary(const char *boundary);
#define BOUNDARY(name) cetta_durable_test_boundary(name)
#else
#define BOUNDARY(name) ((void)0)
#endif

static CettaDurableStatus status(int rc) {
    switch (rc & 255) {
    case SQLITE_OK: case SQLITE_DONE: return DURABLE_OK;
    case SQLITE_BUSY: case SQLITE_LOCKED: return DURABLE_BUSY;
    case SQLITE_NOMEM: return DURABLE_NOMEM;
    case SQLITE_FULL: case SQLITE_TOOBIG: return DURABLE_LIMIT;
    case SQLITE_CORRUPT: case SQLITE_NOTADB: return DURABLE_CORRUPT;
    case SQLITE_CONSTRAINT: return DURABLE_PRECONDITION;
    default: return DURABLE_IO;
    }
}

/* An I/O failure can leave the connection's cache ahead of durable storage.
 * Closing/reopening is mandatory; a failed COMMIT is not a negative receipt. */
static CettaDurableStatus store_status(CettaDurableStore *s, int rc, bool committing) {
    int primary = rc & 255;
    if (primary == SQLITE_IOERR || primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB)
        s->poisoned = true;
    return committing && primary == SQLITE_IOERR ? DURABLE_UNKNOWN : status(rc);
}

static void rollback(CettaDurableStore *s) {
    if (!sqlite3_get_autocommit(s->db) &&
        sqlite3_exec(s->db, "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK)
        s->poisoned = true;
}

/* Shared by recovery and the full, byte-for-byte integrity check at open. */
#define REPLAY_CTE \
    "WITH versions AS (SELECT space,key,value,revision,position,1 AS kind FROM checkpoint" \
    " UNION ALL SELECT space,key,value,revision,position,kind FROM deltas)," \
    " ranked AS (SELECT *,row_number() OVER (PARTITION BY space,key ORDER BY revision DESC) AS n" \
    " FROM versions), replay AS (SELECT space,key,value,revision,position FROM ranked WHERE n=1 AND kind<>3) "

const char *cetta_durable_status_name(CettaDurableStatus s) {
    static const char *names[] = { "ok", "invalid", "busy", "conflict",
        "precondition", "limit", "allocation", "io", "corrupt", "version",
        "unknown-outcome", "poisoned" };
    return (unsigned)s < sizeof(names)/sizeof(*names) ? names[s] : "invalid";
}

CettaDurableLimits cetta_durable_default_limits(void) {
    return (CettaDurableLimits){
        .record_bytes = 1024*1024, .batch_bytes = 4*1024*1024,
        .live_bytes = 64*1024*1024, .history_bytes = 16*1024*1024,
        .records = 100000, .operations = 4096, .database_pages = 65536
    };
}

CettaDurableStatus cetta_durable_attach_runtime(CettaDurableStore *s, const void *owner) {
    if (!s || !owner) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus r=s->poisoned?DURABLE_POISONED:s->runtime_owner?DURABLE_BUSY:DURABLE_OK;
    if (r==DURABLE_OK) s->runtime_owner=owner;
    pthread_mutex_unlock(&s->mutex); return r;
}
CettaDurableStatus cetta_durable_detach_runtime(CettaDurableStore *s, const void *owner) {
    if (!s || !owner) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus r=s->runtime_owner==owner?DURABLE_OK:DURABLE_INVALID;
    if (r==DURABLE_OK) s->runtime_owner=NULL;
    pthread_mutex_unlock(&s->mutex); return r;
}

static bool valid_name(const char *s) {
    return s && *s && strnlen(s, DURABLE_NAME_BYTES+1) <= DURABLE_NAME_BYTES;
}

static int exec(CettaDurableStore *s, const char *sql) {
    return sqlite3_exec(s->db, sql, NULL, NULL, NULL);
}

static int scalar(CettaDurableStore *s, const char *sql, int64_t *value) {
    sqlite3_stmt *q = NULL;
    int rc = sqlite3_prepare_v2(s->db, sql, -1, &q, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(q);
        if (rc == SQLITE_ROW) { *value = sqlite3_column_int64(q, 0); rc = SQLITE_OK; }
        else if (rc == SQLITE_DONE) rc = SQLITE_CORRUPT;
    }
    sqlite3_finalize(q);
    return rc;
}

static int meta(CettaDurableStore *s, char epoch[33], int64_t *rev,
                int64_t *bytes, int64_t *records, int64_t *history) {
    sqlite3_stmt *q = NULL;
    int rc = sqlite3_prepare_v2(s->db,
        "SELECT epoch,revision,live_bytes,live_count,history_bytes FROM meta WHERE id=1",
        -1, &q, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(q);
        if (rc == SQLITE_ROW && sqlite3_column_bytes(q,0) == 32) {
            memcpy(epoch, sqlite3_column_text(q,0),32); epoch[32] = 0;
            *rev = sqlite3_column_int64(q,1);
            *bytes = sqlite3_column_int64(q,2);
            *records = sqlite3_column_int64(q,3);
            *history = sqlite3_column_int64(q,4);
            rc = (*rev >= 0 && *bytes >= 0 && *records >= 0 && *history >= 0)
                ? SQLITE_OK : SQLITE_CORRUPT;
        } else if (rc == SQLITE_ROW || rc == SQLITE_DONE) rc = SQLITE_CORRUPT;
    }
    sqlite3_finalize(q);
    return rc;
}

static int sync_parent(const char *path) {
    char *dir = strdup(path);
    if (!dir) return -1;
    char *slash = strrchr(dir, '/');
    if (slash) { if (slash == dir) slash[1] = 0; else *slash = 0; }
    else { free(dir); dir = strdup("."); if (!dir) return -1; }
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    free(dir);
    int rc = fd < 0 ? -1 : fsync(fd);
    if (fd >= 0) close(fd);
    return rc;
}

CettaDurableStatus cetta_durable_open(const char *path,
        const CettaDurableLimits *limits, CettaDurableStore **out) {
    if (!out) return DURABLE_INVALID;
    *out = NULL;
    if (!path || !*path || !strcmp(path, ":memory:")) return DURABLE_INVALID;
    if (!sqlite3_threadsafe()) return DURABLE_INVALID;
    CettaDurableLimits l = limits ? *limits : cetta_durable_default_limits();
    if (!l.record_bytes || !l.batch_bytes || !l.live_bytes || !l.history_bytes ||
        !l.records || !l.operations || l.database_pages < 64 ||
        l.record_bytes > l.batch_bytes || l.record_bytes > INT_MAX-1024 ||
        l.batch_bytes > l.history_bytes ||
        l.history_bytes > INT_MAX || l.live_bytes > INT_MAX ||
        l.records > INT_MAX || l.operations > INT_MAX) return DURABLE_INVALID;
    CettaDurableStore *s = calloc(1, sizeof(*s));
    if (!s) return DURABLE_NOMEM;
    s->lock_fd = -1; s->limits = l;
    if (pthread_mutex_init(&s->mutex, NULL)) { free(s); return DURABLE_NOMEM; }
    s->lock_fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat st;
    CettaDurableStatus result = DURABLE_IO;
    if (s->lock_fd < 0 || fstat(s->lock_fd, &st) || !S_ISREG(st.st_mode)) goto fail;
    if (flock(s->lock_fd, LOCK_EX | LOCK_NB)) {
        result = errno == EWOULDBLOCK ? DURABLE_BUSY : DURABLE_IO; goto fail;
    }
    int rc = sqlite3_open_v2(path, &s->db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_NOFOLLOW, NULL);
    if (rc != SQLITE_OK) { result = status(rc); goto fail; }
    sqlite3_extended_result_codes(s->db, 1);
    sqlite3_busy_timeout(s->db, 0);
    sqlite3_db_config(s->db, SQLITE_DBCONFIG_DEFENSIVE, 1, NULL);
    sqlite3_db_config(s->db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, NULL);
    /* Admission limits do not lower SQLite's decoding limit: existing data
     * must remain readable after an operator reduces the admission budget. */
    int64_t app = 0, version = 0, tables = 0;
    if ((rc = scalar(s, "PRAGMA application_id", &app)) != SQLITE_OK ||
        (rc = scalar(s, "PRAGMA user_version", &version)) != SQLITE_OK ||
        (rc = scalar(s, "SELECT count(*) FROM sqlite_schema", &tables)) != SQLITE_OK) {
        result = status(rc); goto fail;
    }
    if ((app != DURABLE_APPLICATION_ID || version != DURABLE_SCHEMA) &&
        (app != 0 || version != 0 || tables != 0)) { result = DURABLE_VERSION; goto fail; }
    /* Enforce WAL instead of silently accepting a filesystem/provider fallback. */
    sqlite3_stmt *q = NULL;
    rc = sqlite3_prepare_v2(s->db, "PRAGMA journal_mode=WAL", -1, &q, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(q);
        if (rc == SQLITE_ROW && !strcmp((const char *)sqlite3_column_text(q,0), "wal"))
            rc = SQLITE_OK;
        else if (rc == SQLITE_ROW) rc = SQLITE_IOERR;
    }
    sqlite3_finalize(q);
    if (rc != SQLITE_OK) { result = status(rc); goto fail; }
    rc = exec(s, "PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;"
        "PRAGMA wal_autocheckpoint=256; PRAGMA journal_size_limit=1048576;"
        "PRAGMA temp_store=MEMORY; PRAGMA cache_size=-2048;");
    if (rc != SQLITE_OK) { result = status(rc); goto fail; }
    char *setting = sqlite3_mprintf("PRAGMA max_page_count=%u", l.database_pages);
    if (!setting) { result = DURABLE_NOMEM; goto fail; }
    int64_t actual;
    rc = scalar(s, setting, &actual); sqlite3_free(setting);
    if (rc != SQLITE_OK) { result = status(rc); goto fail; }
    if (!tables) {
        rc = exec(s,
            "BEGIN IMMEDIATE;"
            "CREATE TABLE meta(id INTEGER PRIMARY KEY CHECK(id=1),epoch TEXT NOT NULL,"
            " revision INTEGER NOT NULL,checkpoint_revision INTEGER NOT NULL,"
            " live_bytes INTEGER NOT NULL,live_count INTEGER NOT NULL,history_bytes INTEGER NOT NULL);"
            "INSERT INTO meta VALUES(1,lower(hex(randomblob(16))),0,0,0,0,0);"
            "CREATE TABLE records(space TEXT NOT NULL,key TEXT NOT NULL,value BLOB NOT NULL,"
            " revision INTEGER NOT NULL,position INTEGER NOT NULL,PRIMARY KEY(space,key)) WITHOUT ROWID;"
            "CREATE INDEX record_order ON records(space,revision,position);"
            "CREATE TABLE checkpoint(space TEXT NOT NULL,key TEXT NOT NULL,value BLOB NOT NULL,"
            " revision INTEGER NOT NULL,position INTEGER NOT NULL,PRIMARY KEY(space,key)) WITHOUT ROWID;"
            "CREATE TABLE deltas(revision INTEGER NOT NULL,position INTEGER NOT NULL,kind INTEGER NOT NULL,"
            " space TEXT NOT NULL,key TEXT NOT NULL,value BLOB NOT NULL,PRIMARY KEY(revision,position),"
            " UNIQUE(revision,space,key)) WITHOUT ROWID;"
            "PRAGMA application_id=1129595986; PRAGMA user_version=1; COMMIT;");
        if (rc != SQLITE_OK) { result = status(rc); goto fail; }
    }
    /* Both representations and accounting must describe the same committed
     * state. This is an exact comparison, not a probabilistic checksum. */
    rc = exec(s, "BEGIN");
    int64_t invalid = 0;
    if (rc == SQLITE_OK) rc = scalar(s, REPLAY_CTE
        "SELECT EXISTS(SELECT * FROM records EXCEPT SELECT * FROM replay)"
        " OR EXISTS(SELECT * FROM replay EXCEPT SELECT * FROM records)"
        " OR NOT EXISTS(SELECT 1 FROM meta WHERE id=1 AND length(CAST(epoch AS BLOB))=32 AND epoch NOT GLOB '*[^0-9a-f]*'"
        " AND revision>=0 AND checkpoint_revision BETWEEN 0 AND revision"
        " AND live_count=(SELECT count(*) FROM records)"
        " AND live_bytes=(SELECT coalesce(sum(length(value)+length(CAST(space AS BLOB))+length(CAST(key AS BLOB))),0) FROM records)"
        " AND history_bytes=(SELECT coalesce(sum(length(value)+length(CAST(space AS BLOB))+length(CAST(key AS BLOB))),0) FROM deltas))"
        " OR EXISTS(SELECT 1 FROM deltas,meta WHERE deltas.revision<=checkpoint_revision"
        " OR deltas.revision>meta.revision OR kind NOT IN (1,2,3))"
        " OR EXISTS(SELECT 1 FROM records,meta WHERE records.revision<1 OR records.revision>meta.revision"
        " OR position<0 OR instr(space,char(0))>0 OR instr(key,char(0))>0 OR length(CAST(space AS BLOB)) NOT BETWEEN 1 AND 255"
        " OR length(CAST(key AS BLOB)) NOT BETWEEN 1 AND 255)", &invalid);
    if (rc == SQLITE_OK) rc = exec(s, "COMMIT");
    if (rc != SQLITE_OK || invalid) {
        rollback(s); result = invalid ? DURABLE_CORRUPT : status(rc); goto fail;
    }
    if (sync_parent(path)) goto fail;
    *out = s;
    return DURABLE_OK;
fail:
    cetta_durable_close(s);
    return result;
}

void cetta_durable_close(CettaDurableStore *s) {
    if (!s) return;
    if (s->db) sqlite3_close(s->db);
    if (s->lock_fd >= 0) close(s->lock_fd);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}

void cetta_durable_snapshot_free(CettaDurableSnapshot *v) {
    if (!v) return;
    for (size_t i = 0; i < v->count; ++i) {
        free(v->records[i].space); free(v->records[i].key); free(v->records[i].data);
    }
    free(v->records); memset(v, 0, sizeof(*v));
}

/* The same indexed range and ordering define both the observation and its
 * validation. Prefixes use binary byte bounds, never LIKE's wildcard rules. */
static int prepare_records(CettaDurableStore *s, const char *space,
        const CettaDurableScope *scope, bool metadata_only, sqlite3_stmt **q) {
    unsigned char upper[DURABLE_NAME_BYTES+1]; size_t upper_size=0;
    const char *where=space ? " WHERE space=?1" : "";
    if (scope && scope->kind==DURABLE_KEY) where=" WHERE space=?1 AND key=?2";
    if (scope && scope->kind==DURABLE_PREFIX) {
        upper_size=strlen(scope->key); memcpy(upper,scope->key,upper_size);
        while (upper_size && upper[upper_size-1]==255) --upper_size;
        if (upper_size) ++upper[upper_size-1];
        where=upper_size ? " WHERE space=?1 AND key>=?2 AND key<?3"
                         : " WHERE space=?1 AND key>=?2";
    }
    char *sql=sqlite3_mprintf("SELECT %s FROM records%s ORDER BY revision,position,space,key",
        metadata_only ? "key,revision" : "space,key,value,revision,position",where);
    if (!sql) return SQLITE_NOMEM;
    int rc=sqlite3_prepare_v2(s->db,sql,-1,q,NULL); sqlite3_free(sql);
    if (rc==SQLITE_OK && space) rc=sqlite3_bind_text(*q,1,space,-1,SQLITE_STATIC);
    if (rc==SQLITE_OK && scope && scope->kind!=DURABLE_SPACE)
        rc=sqlite3_bind_text(*q,2,scope->key,-1,SQLITE_STATIC);
    if (rc==SQLITE_OK && upper_size)
        rc=sqlite3_bind_text(*q,3,(const char *)upper,(int)upper_size,SQLITE_TRANSIENT);
    return rc;
}

static CettaDurableStatus snapshot_locked(CettaDurableStore *s, const char *space,
        bool recovery, const CettaDurableScope *scope, CettaDurableSnapshot *out) {
    memset(out, 0, sizeof(*out));
    int64_t bytes, count, history;
    int rc = meta(s, out->epoch, &out->revision, &bytes, &count, &history);
    if (rc != SQLITE_OK) return store_status(s,rc,false);
    BOUNDARY("read_meta");
    /* Selecting the last delta for each occurrence is replay as data. No
     * application code or effect handler runs while reconstructing a view. */
    sqlite3_stmt *q = NULL;
    rc = recovery ? sqlite3_prepare_v2(s->db,
        REPLAY_CTE "SELECT * FROM replay ORDER BY revision,position,space,key",-1,&q,NULL)
        : prepare_records(s,space,scope,false,&q);
    if (rc != SQLITE_OK) { sqlite3_finalize(q); return store_status(s,rc,false); }
    size_t capacity = 0, total = 0;
    while (rc == SQLITE_OK && (rc = sqlite3_step(q)) == SQLITE_ROW) {
        if (out->count >= (uint64_t)count) { rc = SQLITE_CORRUPT; break; }
        int n = sqlite3_column_bytes(q, 2);
        size_t names = (size_t)sqlite3_column_bytes(q,0) + (size_t)sqlite3_column_bytes(q,1);
        if (total + n + names > (uint64_t)bytes) {
            rc = SQLITE_CORRUPT; break;
        }
        if (out->count == capacity) {
            size_t next = capacity ? capacity*2 : 16;
            CettaDurableRecord *rows = realloc(out->records, next*sizeof(*rows));
            if (!rows) { rc = SQLITE_NOMEM; break; }
            out->records = rows; capacity = next;
        }
        CettaDurableRecord *r = &out->records[out->count++];
        *r = (CettaDurableRecord){
            .space = strdup((const char *)sqlite3_column_text(q,0)),
            .key = strdup((const char *)sqlite3_column_text(q,1)),
            .size = (size_t)n, .data = malloc(n ? (size_t)n : 1),
            .revision = sqlite3_column_int64(q,3), .position = sqlite3_column_int64(q,4)
        };
        if (!r->space || !r->key || !r->data) { rc = SQLITE_NOMEM; break; }
        if (n) memcpy(r->data, sqlite3_column_blob(q,2), (size_t)n);
        total += n + names; rc = SQLITE_OK;
    }
    sqlite3_finalize(q);
    CettaDurableStatus result = store_status(s,rc,false);
    if (result != DURABLE_OK) cetta_durable_snapshot_free(out);
    return result;
}

static CettaDurableStatus read_snapshot(CettaDurableStore *s, const char *space,
        bool recovery, CettaDurableSnapshot *out) {
    memset(out,0,sizeof(*out));
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus result = s->poisoned ? DURABLE_POISONED : store_status(s,exec(s,"BEGIN"),false);
    if (result == DURABLE_OK) result = snapshot_locked(s,space,recovery,NULL,out);
    if (result == DURABLE_OK) result = store_status(s,exec(s,"COMMIT"),false);
    rollback(s);
    if (result != DURABLE_OK) cetta_durable_snapshot_free(out);
    pthread_mutex_unlock(&s->mutex);
    return result;
}

CettaDurableStatus cetta_durable_snapshot(CettaDurableStore *s, const char *space,
        CettaDurableSnapshot *out) {
    if (!s || !out || (space && !valid_name(space))) return DURABLE_INVALID;
    return read_snapshot(s,space,false,out);
}

CettaDurableStatus cetta_durable_recover(CettaDurableStore *s, CettaDurableSnapshot *out) {
    if (!s || !out) return DURABLE_INVALID;
    return read_snapshot(s,NULL,true,out);
}

CettaDurableStatus cetta_durable_usage(CettaDurableStore *s, CettaDurableUsage *out) {
    if (!s || !out) return DURABLE_INVALID;
    memset(out,0,sizeof(*out));
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus result = s->poisoned ? DURABLE_POISONED : store_status(s,exec(s,"BEGIN"),false);
    char epoch[33]; int64_t rev, largest=0;
    int rc=SQLITE_OK;
    if (result == DURABLE_OK) rc=meta(s,epoch,&rev,&out->live_bytes,&out->records,&out->history_bytes);
    if (result == DURABLE_OK && rc == SQLITE_OK) rc=scalar(s,"PRAGMA page_count",&out->database_pages);
    if (result == DURABLE_OK && rc == SQLITE_OK) rc=scalar(s,"SELECT coalesce(max(length(value)),0) FROM records",&largest);
    if (result == DURABLE_OK && rc == SQLITE_OK) rc=exec(s,"COMMIT");
    if (result == DURABLE_OK) result=store_status(s,rc,false);
    if (result == DURABLE_OK) out->limits_exceeded =
        (uint64_t)out->live_bytes>s->limits.live_bytes || (uint64_t)out->records>s->limits.records ||
        (uint64_t)out->history_bytes>s->limits.history_bytes || (uint64_t)largest>s->limits.record_bytes ||
        (uint64_t)out->database_pages>s->limits.database_pages;
    rollback(s);
    pthread_mutex_unlock(&s->mutex);
    return result;
}

struct CettaDurableObservation {
    size_t count;
    CettaDurableScope *scopes;
    CettaDurableSnapshot *views;
};

void cetta_durable_observation_free(CettaDurableObservation *o) {
    if (!o) return;
    for (size_t i=0;i<o->count;++i) {
        if (o->scopes) { free((char *)o->scopes[i].space); free((char *)o->scopes[i].key); }
        if (o->views) cetta_durable_snapshot_free(&o->views[i]);
    }
    free(o->scopes); free(o->views); free(o);
}

const CettaDurableSnapshot *cetta_durable_observation_view(const CettaDurableObservation *o, size_t i) {
    return o && i<o->count ? &o->views[i] : NULL;
}

CettaDurableStatus cetta_durable_observe(CettaDurableStore *s,
        const CettaDurableScope *scopes, size_t count, CettaDurableObservation **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!s || !scopes || !count) return DURABLE_INVALID;
    if (count>s->limits.operations) return DURABLE_LIMIT;
    for (size_t i=0;i<count;++i) {
        const CettaDurableScope *q=&scopes[i];
        if (!valid_name(q->space) || q->kind<DURABLE_KEY || q->kind>DURABLE_SPACE ||
            (q->kind==DURABLE_KEY && !valid_name(q->key)) ||
            (q->kind==DURABLE_PREFIX && (!q->key || strnlen(q->key,256)>255))) return DURABLE_INVALID;
    }
    CettaDurableObservation *o=calloc(1,sizeof(*o));
    if (!o) return DURABLE_NOMEM;
    o->count=count; o->scopes=calloc(count,sizeof(*o->scopes)); o->views=calloc(count,sizeof(*o->views));
    if (!o->scopes || !o->views) { cetta_durable_observation_free(o); return DURABLE_NOMEM; }
    for (size_t i=0;i<count;++i) {
        o->scopes[i]=(CettaDurableScope){scopes[i].kind,strdup(scopes[i].space),
            strdup(scopes[i].kind==DURABLE_SPACE ? "" : scopes[i].key)};
        if (!o->scopes[i].space || !o->scopes[i].key) { cetta_durable_observation_free(o); return DURABLE_NOMEM; }
    }
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus result=s->poisoned ? DURABLE_POISONED : store_status(s,exec(s,"BEGIN"),false);
    size_t used=0;
    for (size_t i=0;i<count && result==DURABLE_OK;++i) {
        const CettaDurableScope *q=&o->scopes[i];
        result=snapshot_locked(s,q->space,false,q->kind==DURABLE_SPACE ? NULL : q,&o->views[i]);
        /* Observations are for decisions, not bulk export. Bound overlapping
         * scopes too; snapshot/read remains available for degraded recovery. */
        for (size_t j=0;j<o->views[i].count && result==DURABLE_OK;++j) {
            CettaDurableRecord *r=&o->views[i].records[j];
            size_t n=r->size+strlen(r->space)+strlen(r->key)+sizeof(*r);
            if (n>s->limits.live_bytes-used) result=DURABLE_LIMIT; else used+=n;
        }
    }
    if (result==DURABLE_OK) result=store_status(s,exec(s,"COMMIT"),false);
    rollback(s); pthread_mutex_unlock(&s->mutex);
    if (result==DURABLE_OK) *out=o; else cetta_durable_observation_free(o);
    return result;
}

static bool scope_contains(const CettaDurableScope *q, const CettaDurableOp *op) {
    return !strcmp(q->space,op->space) && (q->kind==DURABLE_SPACE ||
        (q->kind==DURABLE_KEY ? !strcmp(q->key,op->key) : !strncmp(q->key,op->key,strlen(q->key))));
}

static CettaDurableStatus validate_observation(CettaDurableStore *s, const CettaDurableObservation *o) {
    for (size_t i=0;i<o->count;++i) {
        const CettaDurableScope *scope=&o->scopes[i];
        const CettaDurableSnapshot *v=&o->views[i];
        sqlite3_stmt *q=NULL;
        int rc=prepare_records(s,scope->space,scope,true,&q);
        size_t j=0; bool conflict=false;
        while (rc==SQLITE_OK && (rc=sqlite3_step(q))==SQLITE_ROW) {
            if (j>=v->count || strcmp(v->records[j].key,(const char *)sqlite3_column_text(q,0)) ||
                v->records[j].revision!=sqlite3_column_int64(q,1)) { conflict=true; break; }
            ++j; rc=SQLITE_OK;
        }
        sqlite3_finalize(q);
        if (conflict) return DURABLE_CONFLICT;
        if (rc!=SQLITE_DONE) return store_status(s,rc,false);
        if (j!=v->count) return DURABLE_CONFLICT;
    }
    return DURABLE_OK;
}

struct CettaDurableWatch { CettaDurableObservation *observation; size_t bytes; };
void cetta_durable_watch_free(CettaDurableWatch *w) {
    if (w) { cetta_durable_observation_free(w->observation); free(w); }
}
size_t cetta_durable_watch_bytes(const CettaDurableWatch *w) { return w?w->bytes:0; }
CettaDurableStatus cetta_durable_watch_new(const CettaDurableObservation *o,
        size_t limit, CettaDurableWatch **out) {
    if (!out) return DURABLE_INVALID;
    *out=NULL;
    if (!o || !o->count) return DURABLE_INVALID;
    size_t used=sizeof(CettaDurableWatch)+sizeof(*o);
    if (used>limit || o->count>(limit-used)/(sizeof(*o->scopes)+sizeof(*o->views))) return DURABLE_LIMIT;
    used+=o->count*(sizeof(*o->scopes)+sizeof(*o->views));
    for (size_t i=0;i<o->count;++i) {
        size_t n=strlen(o->scopes[i].space)+strlen(o->scopes[i].key)+2;
        if (n>limit-used) return DURABLE_LIMIT;
        used+=n;
        if (o->views[i].count>(limit-used)/sizeof(CettaDurableRecord)) return DURABLE_LIMIT;
        used+=o->views[i].count*sizeof(CettaDurableRecord);
        for (size_t j=0;j<o->views[i].count;++j) {
            n=strlen(o->views[i].records[j].key)+1;
            if (n>limit-used) return DURABLE_LIMIT;
            used+=n;
        }
    }
    CettaDurableWatch *w=calloc(1,sizeof(*w));
    if (!w) return DURABLE_NOMEM;
    w->observation=calloc(1,sizeof(*o)); w->bytes=used;
    if (!w->observation) { cetta_durable_watch_free(w); return DURABLE_NOMEM; }
    CettaDurableObservation *copy=w->observation;
    copy->count=o->count; copy->scopes=calloc(o->count,sizeof(*copy->scopes)); copy->views=calloc(o->count,sizeof(*copy->views));
    if (!copy->scopes || !copy->views) goto nomem;
    for (size_t i=0;i<o->count;++i) {
        copy->scopes[i]=(CettaDurableScope){o->scopes[i].kind,strdup(o->scopes[i].space),strdup(o->scopes[i].key)};
        if (!copy->scopes[i].space || !copy->scopes[i].key) goto nomem;
        CettaDurableSnapshot *v=&copy->views[i]; const CettaDurableSnapshot *src=&o->views[i];
        memcpy(v->epoch,src->epoch,sizeof(v->epoch)); v->revision=src->revision;
        v->records=src->count?calloc(src->count,sizeof(*v->records)):NULL;
        if (src->count && !v->records) goto nomem;
        for (size_t j=0;j<src->count;++j) {
            v->records[j].key=strdup(src->records[j].key); v->records[j].revision=src->records[j].revision;
            ++v->count;
            if (!v->records[j].key) goto nomem;
        }
    }
    *out=w; return DURABLE_OK;
nomem:
    cetta_durable_watch_free(w); return DURABLE_NOMEM;
}
CettaDurableStatus cetta_durable_watch_current(CettaDurableStore *s, const CettaDurableWatch *w) {
    if (!s || !w) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    CettaDurableStatus result=s->poisoned?DURABLE_POISONED:store_status(s,exec(s,"BEGIN"),false);
    char epoch[33]; int64_t rev,bytes,records,history;
    if (result==DURABLE_OK) result=store_status(s,meta(s,epoch,&rev,&bytes,&records,&history),false);
    if (result==DURABLE_OK && strcmp(epoch,w->observation->views[0].epoch)) result=DURABLE_CONFLICT;
    if (result==DURABLE_OK) result=validate_observation(s,w->observation);
    if (result==DURABLE_OK) result=store_status(s,exec(s,"COMMIT"),false);
    rollback(s); pthread_mutex_unlock(&s->mutex); return result;
}

static int checkpoint_locked(CettaDurableStore *s) {
    return exec(s, "DELETE FROM checkpoint; INSERT INTO checkpoint SELECT * FROM records;"
        "DELETE FROM deltas; UPDATE meta SET checkpoint_revision=revision,history_bytes=0 WHERE id=1;");
}

CettaDurableStatus cetta_durable_checkpoint(CettaDurableStore *s) {
    if (!s) return DURABLE_INVALID;
    pthread_mutex_lock(&s->mutex);
    if (s->poisoned) { pthread_mutex_unlock(&s->mutex); return DURABLE_POISONED; }
    int rc = exec(s, "BEGIN IMMEDIATE");
    bool committing = false;
    if (rc == SQLITE_OK) rc = checkpoint_locked(s);
    if (rc == SQLITE_OK) { BOUNDARY("checkpoint"); committing=true; rc = exec(s, "COMMIT"); }
    CettaDurableStatus result=store_status(s,rc,committing);
    rollback(s);
    /* No long-lived reader transactions are exposed, so a successful commit
     * can reclaim the WAL. A busy checkpoint does not undo that commit. */
    if (rc == SQLITE_OK) {
        int checkpoint_rc=sqlite3_wal_checkpoint_v2(s->db, NULL, SQLITE_CHECKPOINT_TRUNCATE, NULL, NULL);
        if (checkpoint_rc != SQLITE_BUSY && checkpoint_rc != SQLITE_OK)
            result=store_status(s,checkpoint_rc,false);
    }
    pthread_mutex_unlock(&s->mutex);
    return result;
}

static int bind_key(sqlite3_stmt *q, const CettaDurableOp *op) {
    int rc = sqlite3_bind_text(q,1,op->space,-1,SQLITE_STATIC);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(q,2,op->key,-1,SQLITE_STATIC);
    return rc;
}

static CettaDurableStatus commit_internal(CettaDurableStore *s,
        const char *expected_epoch, int64_t expected_revision, const CettaDurableObservation *observation,
        const CettaDurableOp *ops, size_t count, int64_t *published_revision) {
    if (published_revision) *published_revision = -1;
    if (!s || !expected_epoch || strlen(expected_epoch) != 32 ||
        expected_revision < 0 || !ops || !count || !published_revision) return DURABLE_INVALID;
    if (count > s->limits.operations) return DURABLE_LIMIT;
    size_t batch = 0;
    bool removals_only = true;
    for (size_t i=0; i<count; ++i) {
        const CettaDurableOp *op = &ops[i];
        if (!valid_name(op->space) || !valid_name(op->key) ||
            op->kind < DURABLE_INSERT || op->kind > DURABLE_REMOVE ||
            (op->size && !op->data) || (op->kind == DURABLE_REMOVE && op->size))
            return DURABLE_INVALID;
        if (observation) {
            bool covered=false;
            for (size_t j=0;j<observation->count;++j) if (scope_contains(&observation->scopes[j],op)) { covered=true; break; }
            if (!covered) return DURABLE_INVALID;
        }
        if (op->size > s->limits.record_bytes) return DURABLE_LIMIT;
        size_t n = op->size + strlen(op->space) + strlen(op->key);
        if (n > SIZE_MAX - batch) return DURABLE_LIMIT;
        batch += n;
        if (op->kind != DURABLE_REMOVE) removals_only=false;
    }
    /* Old names can exceed a newly lowered byte limit. A deletion-only batch
     * is still bounded by the operation cap and the fixed name-size cap. */
    if (!removals_only && batch>s->limits.batch_bytes) return DURABLE_LIMIT;
    pthread_mutex_lock(&s->mutex);
    if (s->poisoned) { pthread_mutex_unlock(&s->mutex); return DURABLE_POISONED; }
    int rc = exec(s, "BEGIN IMMEDIATE");
    CettaDurableStatus result = store_status(s,rc,false);
    bool committing = false;
    sqlite3_stmt *read = NULL, *write = NULL, *log = NULL, *m = NULL;
    char epoch[33]; int64_t rev = -1, bytes, rows, history;
    if (rc != SQLITE_OK) goto done;
    rc = meta(s, epoch, &rev, &bytes, &rows, &history);
    if (rc != SQLITE_OK) goto sql_error;
    if (strcmp(epoch, expected_epoch) || (!observation && rev != expected_revision)) { result = DURABLE_CONFLICT; goto done; }
    if (observation) {
        result=validate_observation(s,observation);
        if (result!=DURABLE_OK) goto done;
    }
    if (rev == INT64_MAX) { result = DURABLE_LIMIT; goto done; }
    if ((uint64_t)history > INT64_MAX-batch) { result=DURABLE_LIMIT; goto done; }
    /* Removing existing occurrences cannot grow this reserve without bound:
     * each name is logged once until a later growth batch checkpoints first.
     * Do not copy the live set just to make room for a deletion. */
    if (!removals_only && (uint64_t)history + batch > s->limits.history_bytes) {
        rc = checkpoint_locked(s); if (rc != SQLITE_OK) goto sql_error;
        history = 0;
    }
    rc = sqlite3_prepare_v2(s->db, "SELECT length(value) FROM records WHERE space=?1 AND key=?2", -1,&read,NULL);
    if (rc != SQLITE_OK) goto sql_error;
    rc = sqlite3_prepare_v2(s->db, "INSERT INTO deltas VALUES(?1,?2,?3,?4,?5,?6)", -1,&log,NULL);
    if (rc != SQLITE_OK) goto sql_error;
    for (size_t i=0; i<count; ++i) {
        const CettaDurableOp *op = &ops[i];
        sqlite3_reset(read);
        rc = bind_key(read,op); if (rc != SQLITE_OK) goto sql_error;
        rc = sqlite3_step(read);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) goto sql_error;
        bool exists = rc == SQLITE_ROW;
        if (exists == (op->kind == DURABLE_INSERT)) { result = DURABLE_PRECONDITION; goto done; }
        int64_t names = (int64_t)(strlen(op->space)+strlen(op->key));
        int64_t previous_bytes=bytes, previous_rows=rows;
        if (exists) { bytes -= sqlite3_column_int64(read,0) + names; --rows; }
        if (op->kind != DURABLE_REMOVE) { bytes += (int64_t)op->size + names; ++rows; }
        if (bytes < 0 || rows < 0) { result = DURABLE_CORRUPT; goto done; }
        if (((uint64_t)bytes > s->limits.live_bytes && bytes > previous_bytes) ||
            ((uint64_t)rows > s->limits.records && rows > previous_rows)) {
            result = DURABLE_LIMIT; goto done;
        }
        const char *sql = op->kind == DURABLE_REMOVE
            ? "DELETE FROM records WHERE space=?1 AND key=?2"
            : "INSERT INTO records VALUES(?1,?2,?3,?4,?5) ON CONFLICT(space,key)"
              " DO UPDATE SET value=excluded.value,revision=excluded.revision,position=excluded.position";
        rc = sqlite3_prepare_v2(s->db,sql,-1,&write,NULL); if (rc != SQLITE_OK) goto sql_error;
        rc = bind_key(write,op); if (rc != SQLITE_OK) goto sql_error;
        if (op->kind != DURABLE_REMOVE) {
            rc = sqlite3_bind_blob(write,3,op->size ? op->data : "",(int)op->size,SQLITE_STATIC);
            if (rc == SQLITE_OK) rc = sqlite3_bind_int64(write,4,rev+1);
            if (rc == SQLITE_OK) rc = sqlite3_bind_int64(write,5,(int64_t)i);
            if (rc != SQLITE_OK) goto sql_error;
        }
        rc = sqlite3_step(write); if (rc != SQLITE_DONE) goto sql_error;
        sqlite3_finalize(write); write = NULL;
        sqlite3_reset(log);
        if ((rc=sqlite3_bind_int64(log,1,rev+1)) != SQLITE_OK ||
            (rc=sqlite3_bind_int64(log,2,(int64_t)i)) != SQLITE_OK ||
            (rc=sqlite3_bind_int(log,3,op->kind)) != SQLITE_OK ||
            (rc=sqlite3_bind_text(log,4,op->space,-1,SQLITE_STATIC)) != SQLITE_OK ||
            (rc=sqlite3_bind_text(log,5,op->key,-1,SQLITE_STATIC)) != SQLITE_OK ||
            (rc=sqlite3_bind_blob(log,6,op->size ? op->data : "",(int)op->size,SQLITE_STATIC)) != SQLITE_OK)
            goto sql_error;
        rc = sqlite3_step(log); if (rc != SQLITE_DONE) goto sql_error;
        BOUNDARY("operation");
    }
    rc = sqlite3_prepare_v2(s->db,"UPDATE meta SET revision=?1,live_bytes=?2,live_count=?3,history_bytes=?4 WHERE id=1",-1,&m,NULL);
    if (rc != SQLITE_OK) goto sql_error;
    if ((rc=sqlite3_bind_int64(m,1,rev+1)) != SQLITE_OK ||
        (rc=sqlite3_bind_int64(m,2,bytes)) != SQLITE_OK ||
        (rc=sqlite3_bind_int64(m,3,rows)) != SQLITE_OK ||
        (rc=sqlite3_bind_int64(m,4,history+(int64_t)batch)) != SQLITE_OK) goto sql_error;
    rc = sqlite3_step(m); if (rc != SQLITE_DONE) goto sql_error;
    BOUNDARY("before_commit");
    committing = true;
    rc = exec(s,"COMMIT"); if (rc != SQLITE_OK) goto sql_error;
    BOUNDARY("after_commit");
    *published_revision = rev+1;
    result = DURABLE_OK;
    goto done;
sql_error:
    result = store_status(s,rc,committing);
done:
    sqlite3_finalize(read); sqlite3_finalize(write); sqlite3_finalize(log); sqlite3_finalize(m);
    rollback(s);
    if (result == DURABLE_CORRUPT) s->poisoned=true;
    pthread_mutex_unlock(&s->mutex);
    return result;
}

CettaDurableStatus cetta_durable_commit(CettaDurableStore *s,
        const char *epoch, int64_t revision, const CettaDurableOp *ops,
        size_t count, int64_t *published) {
    return commit_internal(s,epoch,revision,NULL,ops,count,published);
}

CettaDurableStatus cetta_durable_commit_observed(CettaDurableStore *s,
        const CettaDurableObservation *o, const CettaDurableOp *ops,
        size_t count, int64_t *published) {
    if (!o || !o->count) { if (published) *published=-1; return DURABLE_INVALID; }
    return commit_internal(s,o->views[0].epoch,o->views[0].revision,o,ops,count,published);
}
