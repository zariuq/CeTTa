#!/usr/bin/env python3
"""Raw native SQLite parity: values, identities, atomic plans and shared linkage."""
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
Q = lambda value: json.dumps(str(value), ensure_ascii=False)

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
