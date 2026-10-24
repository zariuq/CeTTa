#!/usr/bin/env python3
"""Raw native SQLite parity: values, identities, atomic plans, read-only
observation and shared linkage."""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
Q = lambda value: json.dumps(str(value), ensure_ascii=False)
LANGUAGES = ('he', 'petta', 'prime')

def snapshot(path):
    """The bytes and modification time an observation must leave unchanged."""
    status = path.stat()
    return hashlib.sha256(path.read_bytes()).hexdigest(), status.st_size, status.st_mtime_ns

@unittest.skipUnless(os.environ.get('CETTA_BIN'), 'set CETTA_BIN to a SQLite build')
class NativeSqlite(unittest.TestCase):
    def guest(self, directory, code, lang='petta'):
        source = Path(directory) / 'probe.metta'
        source.write_text('!(import! &self '+('(library sqlite)' if lang=='petta' else 'sqlite')+')\n'+code)
        result = subprocess.run([os.environ['CETTA_BIN'], '--lang', lang, str(source)], cwd=ROOT,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn('Assertion failed', result.stderr)
        self.assertIn('SQLITE_OK', result.stdout, result.stdout + result.stderr)
        return result.stdout

    def test_values_and_existing_rowids_are_unchanged(self):
        for lang in ('petta', 'he'):
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                path = Path(d) / 'history.sqlite3'
                with sqlite3.connect(path) as db:
                    db.execute('CREATE TABLE values_kept (id TEXT PRIMARY KEY, n INTEGER, r REAL, t TEXT, absent TEXT, b BLOB)')
                    db.execute('INSERT INTO values_kept VALUES (?,?,?,?,?,?)', ('owner:7:0', 9223372036854775807, 1.25, 'λ\x00tail', None, b'a\x00b'))
                    before = db.execute('SELECT rowid,* FROM values_kept').fetchall()
                    journal = db.execute('PRAGMA journal_mode').fetchone()[0]
                self.guest(d, f'''!(case (sqlite:query {Q(path)} "SELECT rowid,id,n,r,t,absent,b FROM values_kept" () 10000)
                    (((sqlite:result $columns $rows $_ $_)
                      (progn (assert (== (size-atom $rows) 1))
                        (let ($rowid $id $n $r $t $null $blob) (car-atom $rows)
                          (progn (assert (== $id "owner:7:0")) (assert (== $n 9223372036854775807))
                                 (assert (== $null sqlite:null)) (println! SQLITE_OK)))))))
''', lang)
                with sqlite3.connect(path) as db:
                    self.assertEqual(db.execute('SELECT rowid,* FROM values_kept').fetchall(), before)
                    self.assertEqual(db.execute('PRAGMA journal_mode').fetchone()[0], journal)
                    self.assertEqual(db.execute('PRAGMA application_id').fetchone()[0], 0)
                    self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0], 0)

    def test_atomic_rollback_and_caller_selected_bound_values(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'history.sqlite3'
            with sqlite3.connect(path) as db: db.execute('CREATE TABLE items (id TEXT PRIMARY KEY, value TEXT)')
            self.guest(d, f'''!(case (sqlite:transaction {Q(path)} immediate
              ((sqlite:statement "INSERT INTO items VALUES (?,?)" ("once" "λ"))
               (sqlite:statement "INSERT INTO items VALUES (?,?)" ("once" "conflict"))) 10000)
              (((sqlite:error constraint $_ $_ failed) True)))
!(case (sqlite:query {Q(path)} "SELECT count(*) FROM items" () 10000)
   (((sqlite:result $_ ((0)) $_ $_) True)))
!(case (sqlite:transaction {Q(path)} immediate
              ((sqlite:statement "INSERT INTO items VALUES (?,?)" ("once" "λ"))
               (sqlite:statement "SELECT value FROM items WHERE id=?" ("once"))) 10000)
              (((sqlite:committed $results) (progn (assert (== (size-atom $results) 2)) (println! SQLITE_OK)))))
''')
            with sqlite3.connect(path) as db:
                self.assertEqual(db.execute('SELECT id,value FROM items').fetchall(), [('once','λ')])

    def test_all_rows_are_available_without_a_default_row_quota(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'history.sqlite3'
            with sqlite3.connect(path) as db:
                db.execute('CREATE TABLE rows_kept (id INTEGER)')
                db.executemany('INSERT INTO rows_kept VALUES (?)', [(i,) for i in range(2500)])
            self.guest(d, f'''!(case (sqlite:query {Q(path)} "SELECT id FROM rows_kept ORDER BY id" () 10000)
                (((sqlite:result $_ $rows $_ $_) (progn (assert (== (size-atom $rows) 2500)) (println! SQLITE_OK)))))
''')

    def test_observing_a_missing_database_fails_without_creating_it(self):
        for lang in LANGUAGES:
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                path = Path(d) / 'absent.sqlite3'
                self.guest(d, f'''!(case (sqlite:observe {Q(path)} "SELECT 1" () 1000)
   (((sqlite:error io 14 "database file does not exist" failed) (println! SQLITE_OK))))
''', lang)
                self.assertEqual(sorted(p.name for p in Path(d).iterdir()), ['probe.metta'])

    def test_observation_refuses_statements_that_write_or_attach(self):
        for lang in LANGUAGES:
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                path, other, copy = (Path(d) / name for name in ('history.sqlite3', 'other.sqlite3', 'copy.sqlite3'))
                with sqlite3.connect(path) as db:
                    db.execute('CREATE TABLE items (id TEXT PRIMARY KEY, value TEXT)')
                    db.execute("INSERT INTO items VALUES ('kept', 'λ')")
                db.close()
                before = snapshot(path)
                # Each refusal precedes execution: SQLite reports the prepared
                # statement can write, ATTACH is not authorized, or SQL holds
                # more than one statement.
                writes = (8, 'sqlite:observe runs only statements that do not write')
                refused = [('INSERT INTO items VALUES (?,?)', '("added" "x")', *writes),
                           ('UPDATE items SET value=?', '("changed")', *writes),
                           ('DELETE FROM items', '()', *writes),
                           ('CREATE TABLE created (id INTEGER)', '()', *writes),
                           ('CREATE TEMP TABLE scratch (id INTEGER)', '()', *writes),
                           ('BEGIN IMMEDIATE', '()', *writes),
                           ('PRAGMA user_version=7', '()', *writes),
                           ('PRAGMA journal_mode=WAL', '()', *writes),
                           ('PRAGMA wal_checkpoint(TRUNCATE)', '()', *writes),
                           ('VACUUM INTO ?', f'({Q(copy)})', *writes),
                           ('ATTACH DATABASE ? AS other', f'({Q(other)})', 23, 'sqlite:observe does not attach databases'),
                           ('SELECT 1; DELETE FROM items', '()', 21, 'sqlite:observe runs one statement')]
                result = self.guest(d, ''.join(f'''!(case (sqlite:observe {Q(path)} {Q(sql)} {parameters} 1000)
   (((sqlite:error sql {code} {Q(message)} failed) (println! SQLITE_REFUSED))))
''' for sql, parameters, code, message in refused) + '!(println! SQLITE_OK)\n', lang)
                self.assertEqual(result.count('SQLITE_REFUSED'), len(refused), result)
                self.assertEqual(snapshot(path), before)
                self.assertEqual(sorted(p.name for p in Path(d).iterdir()), ['history.sqlite3', 'probe.metta'])
                with sqlite3.connect(path) as db:
                    self.assertEqual(db.execute('SELECT * FROM items').fetchall(), [('kept', 'λ')])
                    self.assertEqual(db.execute('PRAGMA user_version').fetchone()[0], 0)
                    self.assertEqual(db.execute('PRAGMA journal_mode').fetchone()[0], 'delete')
                db.close()

    def test_observation_reads_committed_wal_state_beside_an_open_writer(self):
        for lang in LANGUAGES:
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                path = Path(d) / 'ledger.sqlite3'
                wal = path.with_name(path.name + '-wal')
                writer = sqlite3.connect(path, isolation_level=None)
                try:
                    self.assertEqual(writer.execute('PRAGMA journal_mode=WAL').fetchone()[0], 'wal')
                    writer.execute('PRAGMA wal_autocheckpoint=0')
                    writer.execute('CREATE TABLE items (id INTEGER PRIMARY KEY, value TEXT)')
                    writer.executemany('INSERT INTO items VALUES (?,?)', [(1, 'one'), (2, 'two')])
                    writer.execute('BEGIN IMMEDIATE')
                    writer.execute("INSERT INTO items VALUES (3, 'uncommitted')")
                    database, log = snapshot(path), snapshot(wal)
                    self.guest(d, f'''!(case (sqlite:observe {Q(path)} "SELECT id,value FROM items ORDER BY id" () 1000)
   (((sqlite:result ("id" "value") ((1 "one") (2 "two")) 0 0) (println! SQLITE_OK))))
''', lang)
                    # Committed frames stay in the WAL: no checkpoint copied them.
                    self.assertEqual(snapshot(path), database)
                    self.assertEqual(snapshot(wal), log)
                    self.assertTrue(writer.in_transaction)
                    writer.execute('COMMIT')
                finally:
                    writer.close()
                # The writer's close checkpointed its WAL; an idle database
                # file is read in place too.
                database = snapshot(path)
                self.guest(d, f'''!(case (sqlite:observe {Q(path)} "SELECT count(*) FROM items" () 1000)
   (((sqlite:result $_ ((3)) 0 0) (println! SQLITE_OK))))
''', lang)
                self.assertEqual(snapshot(path), database)

    def test_observation_binds_parameters(self):
        for lang in LANGUAGES:
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                path = Path(d) / 'history.sqlite3'
                with sqlite3.connect(path) as db:
                    db.execute('CREATE TABLE calls (id TEXT PRIMARY KEY, n INTEGER, r REAL, t TEXT, absent TEXT)')
                    db.executemany('INSERT INTO calls VALUES (?,?,?,?,?)',
                                   [('call-1', 7, 1.25, 'λ', None), ('call-2', 8, 2.5, 'other', 'x')])
                db.close()
                before = snapshot(path)
                result = self.guest(d, f'''!(case (sqlite:observe {Q(path)} "SELECT id FROM calls WHERE id=? AND n=? AND r=? AND t=? AND absent IS ?"
                       ("call-1" 7 1.25 "λ" sqlite:null) 1000)
   (((sqlite:result ("id") (("call-1")) 0 0) (println! SQLITE_BOUND))))
!(case (sqlite:observe {Q(path)} "SELECT typeof(?),typeof(?),typeof(?),typeof(?),typeof(?),typeof(?)"
                       ("t" 1 1.5 sqlite:null (sqlite:blob "b") word) 1000)
   (((sqlite:result $_ (("text" "integer" "real" "null" "blob" "text")) 0 0) (println! SQLITE_BOUND))))
!(case (sqlite:observe {Q(path)} "SELECT id FROM calls WHERE id=?" () 1000)
   (((sqlite:error sql 25 "parameter count does not match SQL" failed) (println! SQLITE_BOUND))))
!(println! SQLITE_OK)
''', lang)
                self.assertEqual(result.count('SQLITE_BOUND'), 3, result)
                self.assertEqual(snapshot(path), before)

    def test_embedded_python_and_native_use_one_shared_sqlite(self):
        linkage = subprocess.check_output(['ldd', os.environ['CETTA_BIN']], text=True)
        self.assertIn('libsqlite3.so', linkage)
        defined = subprocess.check_output(['nm', '-D', '--defined-only', os.environ['CETTA_BIN']], text=True)
        self.assertNotRegex(defined, r'\b[TDB] sqlite3_')
        with tempfile.TemporaryDirectory() as d:
            module = Path(d) / 'sqlite_peer.py'
            module.write_text('import sqlite3\n'
                'def check(path):\n'
                '    with sqlite3.connect(path) as db:\n'
                '        count = db.execute("SELECT count(*) FROM rows_kept").fetchone()[0]\n'
                '    libraries = {line.split()[-1] for line in open("/proc/self/maps") if "libsqlite3.so" in line}\n'
                '    return int(count == 1 and len(libraries) == 1)\n')
            path = Path(d) / 'history.sqlite3'
            self.guest(d, f'''!(import! &self {Q(module)})
!(sqlite:transaction {Q(path)} immediate
  ((sqlite:statement "CREATE TABLE rows_kept (id INTEGER)" ())
   (sqlite:statement "INSERT INTO rows_kept VALUES (1)" ())) 10000)
!(assert (== (py-call (sqlite_peer.check {Q(path)})) 1))
!(println! SQLITE_OK)
''')

if __name__ == '__main__': unittest.main()
