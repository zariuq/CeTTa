/* rc_feed.h - a stream read by another compiled module (the reader) and fed to this one as the
 * records of its record cursor (rep SORT records), while the reader reads.
 *
 * The reader is a compiled module's instance in the same binary, used through its table
 * (rc_instance_api.h), and built on the byte-record library: a record is BRecord OPCODE OPERANDS,
 * each operand BWord N, BBytes S, BWords (WordsCons N ...) or BPairs (PairsCons (BPair N M) ...).  Its
 * entry judgment reads the run's input; its loop over the records is its safepoint, and every record it
 * reads waits in a pending frame of a wrapper (the loop's tail call modulo the wrapper, rc_rt.h), the
 * record one of the wrapper's arguments, until the reader ends and applies the frames.
 *
 * The feed stops the reader at its safepoint whenever a batch of records waits in frames (the stream's
 * header is an argument of the safepoint).  At a stop it takes the records out of their frames (kept
 * here compactly: an opcode and its operands) and drops the frames, so the reader holds no records and
 * collects as it goes; then the reader continues.  A short stream is read to its end at once.  The reader's
 * value, when it ends, is the value its rules give for the input with every taken record's frame
 * removed: the instance's description says what that value means for this module (the stream's end).
 * It is sound when the wrapper conses its record onto the records of every value the loop can end with
 * (stated for each reader in the Lean file of its module).
 *
 * This module reads record i through its cursor: rc_record_more(i) continues the reader only while
 * record i has not been read, so a run that stops early (a stream refused at its tenth record) never
 * makes the reader read further; rc_record_term(i) builds the record's term when the module reaches
 * it: the constructor the description's record table gives for its opcode, its children the operands
 * in order, each in the representation of the child's sort (a word as a natural number, a byte string
 * as a byte string, a list of words or of pairs as a list sort with one nullary constructor and one
 * constructor of one or two numbers and the rest).  The stream's end is a promise (rep SORT promise):
 * forcing it reads the stream to its end.
 *
 * A record that is not of the library's shape, an opcode the table does not name, an operand that does
 * not fit its child, frames above the stream that are not the wrapper's, and a reader that gives no
 * value are refinement violations (rc_refinement_fail): they stop the run, as do the reader's own.
 */
#ifndef RC_FEED_H
#define RC_FEED_H

#include "rc_instance_api.h"

/* a row of the record table: the reader's records of this opcode are this module's constructor */
typedef struct RcFeedRecord { uint16_t opcode; uint16_t ctor; } RcFeedRecord;

typedef struct RcFeed {
    const RcInstanceApi *reader;
    const char *entry;            /* the reader's judgment on the input (its one argument) */
    const char *wrapper;          /* the wrapper whose frames hold the records */
    int element;                  /* the record's position among the wrapper's arguments */
    int header_root;              /* the safepoint's argument that holds the stream's header */
    const char *stream_sort;      /* this module's sort of the value standing for the stream */
    const RcFeedRecord *records;  /* the record table */
    size_t nrecords;
    /* this module's value for the stream, made once the reader has stopped at its loop or ended:
     * HEADER the reader's header (NULL when the reader ended before its loop), FINAL the reader's value
     * when it has ended (else NULL), CURSOR the record cursor at the first record, END the promise of
     * the stream's end */
    struct T *(*stream)(const RcInstanceApi *reader, struct T *header, struct T *final, struct T *cursor, struct T *end);
    /* this module's value for the stream's end, from the reader's value FINAL (when the end is read) */
    struct T *(*end)(const RcInstanceApi *reader, struct T *final);
} RcFeed;

/* Starts the run's feed: the reader reads the input until its first stop (or its end); returns this
 * module's value for the stream, or NULL with *why set when the input cannot be read.  The bytes stay
 * unchanged until rc_feed_end. */
struct T *rc_feed_begin(const RcFeed *f, const uint8_t *bytes, size_t len, const char **why);
/* Reads the stream to its end now (the stream's end is then known: forcing it reads nothing more). */
void rc_feed_finish(void);
/* Ends the run's feed: the reader's run ended (its store kept for the next one, or freed when the run
 * was stopped by a violation), the records forgotten. */
void rc_feed_end(bool stopped);

typedef struct { uint64_t records, stops, read_ns; } RcFeedStats;
RcFeedStats rc_feed_stats(void);   /* for the current (or last) run */

/* the record cursor's side (the generated code of a (rep SORT records) sort reads them): the records
 * read so far; record i once read (false when the stream ends before it); record i as a term */
extern uint64_t rc_record_count;
bool rc_record_more(uint64_t i);
struct T *rc_record_term(uint64_t i);

#endif
