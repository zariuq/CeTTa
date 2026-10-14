#!/usr/bin/env python3
"""Native process/timer ownership, lifecycle, files and mixed-provider wakes."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]

class Peer(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        time.sleep(.3)
        self.send_response(200); self.end_headers(); self.wfile.write(b'http finished')

@unittest.skipUnless(os.environ.get('CETTA_BIN'), 'set CETTA_BIN')
class NativeIoProviders(unittest.TestCase):
    def run_guest(self, code, directory, lang='petta'):
        source = Path(directory) / 'probe.metta'
        source.write_text('!(import! &self '+('(library io)' if lang=='petta' else 'io')+')\n'
                          +'!(import! &self '+('(library fs)' if lang=='petta' else 'fs')+')\n'+code)
        result=subprocess.run([os.environ['CETTA_BIN'],'--lang',lang,str(source)],cwd=ROOT,
                              text=True,capture_output=True,timeout=15)
        self.assertEqual(result.returncode,0,(result.stdout+result.stderr)[-2500:])
        self.assertNotIn('Assertion failed',result.stderr)
        self.assertNotIn('(Error',result.stdout)
        self.assertIn('PROVIDER_OK',result.stdout)
        self.assertNotIn('PROVIDER_MISMATCH', result.stdout)
        return result

    def process(self, directory, command, timeout=0):
        log=json.dumps(str(Path(directory)/'job.log'))
        return '(proc:request ("/bin/sh" "-c" '+json.dumps(command)+') "/" inherit '+str(timeout)+' '+log+' '+log+')'

    def test_owned_timer_preserves_another_completion(self):
        for lang in ('petta','he'):
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                self.run_guest('''!(let* (((io:pending $fast) (io:submit (timer:after 0)))
                      ((io:pending $slow) (io:submit (timer:after 40)))
                      ((io:event $slow (timer:fired $elapsed)) (io:wait $slow 2000)))
                  (progn (assert (>= $elapsed 30))
                         (case (io:poll $slow) (((io:unknown $slow) True)))
                         (case (io:poll $fast) (((io:event $fast (timer:fired $_)) (println! PROVIDER_OK))))))
''',d,lang)

    def test_process_returns_immediately_and_keeps_files(self):
        for lang in ('petta','he'):
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                request=self.process(d,'printf first; sleep .15; printf second >&2; exit 7')
                out=json.dumps(str(Path(d)/'job.log'))
                code=f'''!(let* (((io:pending $id) (io:submit {request}))
                  ((proc:running $pid $elapsed $out $err) (io:status $id)))
                  (progn (assert (< $elapsed 140))
                    (case (io:wait $id 2000)
                      (((io:event $id (proc:finished $pid exited 7 $out $err))
                        (progn (assert (== (fs:read-text $out) "firstsecond"))
                               (case (io:poll $id) (((io:unknown $id) True)))
                               (println! PROVIDER_OK)))))))\n'''
                self.run_guest(code,d,lang)
                self.assertEqual((Path(d)/'job.log').read_text(),'firstsecond')

    def test_timeout_kills_group_and_retains_partial_output(self):
        with tempfile.TemporaryDirectory() as d:
            escaped=Path(d)/'escaped'
            request=self.process(d,f"(sleep .5; printf escaped > '{escaped}') & printf partial; sleep 2",100)
            code=f'''!(let (io:pending $id) (io:submit {request})
              (case (io:wait $id 2000)
                (((io:event $id (proc:finished $pid timed-out 0 $out $err))
                  (progn (assert (== (fs:read-text $out) "partial")) (println! PROVIDER_OK))))))\n'''
            self.run_guest(code,d)
            time.sleep(.6); self.assertFalse(escaped.exists())

    def test_signal_preserves_completion_and_cancel_consumes(self):
        with tempfile.TemporaryDirectory() as d:
            request=self.process(d,'printf started; sleep 5')
            code=f'''!(let (io:pending $id) (io:submit {request})
              (progn (io:signal $id 15)
                (case (io:wait $id 2000)
                  (((io:event $id (proc:finished $pid signaled 15 $out $err)) True)))))
!(let (io:pending $id) (io:submit (timer:after 5000))
   (progn (io:cancel $id) (case (io:poll $id) (((io:unknown $id) True))) (println! PROVIDER_OK)))\n'''
            self.run_guest(code,d)

    def test_deadline_runs_while_evaluator_is_busy(self):
        for lang in ('petta', 'he'):
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                marker = Path(d) / 'descendant-wrote'
                request = self.process(d,
                    f"printf early; (sleep .12; touch '{marker}') & sleep .15; printf late", 50)
                proc_import = '(library proc)' if lang == 'petta' else 'proc'
                self.run_guest(f'''!(import! &self {proc_import})
!(let* (((io:pending $id) (io:submit {request}))
        ($_ (proc:run ("/bin/sh" "-c" "sleep .3") "/" inherit 0 0)))
   (case (io:poll $id)
     (((io:event $id (proc:finished $pid timed-out 0 $out $err))
       (progn (assert (== (fs:read-text $out) "early")) (println! PROVIDER_OK))))))
''', d, lang)
                self.assertFalse(marker.exists())

    def test_start_failures_are_distinct_from_exit_127(self):
        with tempfile.TemporaryDirectory() as d:
            script = Path(d) / 'missing-interpreter'
            script.write_text('#!/no/such/interpreter\n')
            script.chmod(0o700)
            log = json.dumps(str(Path(d) / 'job.log'))
            requests = [
                f'(proc:request ("/bin/true") "/no/such/directory" inherit 0 {log} {log})',
                f'(proc:request ({json.dumps(str(script))}) "/" inherit 0 {log} {log})',
            ]
            code = '\n'.join(f'''!(let (io:pending $id) (io:submit {r})
   (case (io:wait $id 2000)
     (((io:event $id (proc:finished $pid failed 2 $out $err)) (println! START_ERROR_OK)))))'''
                for r in requests)
            code += f'''\n!(let (io:pending $id) (io:submit {self.process(d, 'exit 127')})
   (case (io:wait $id 2000)
     (((io:event $id (proc:finished $pid exited 127 $out $err)) (println! PROVIDER_OK)))))\n'''
            result = self.run_guest(code, d)
            self.assertEqual(result.stdout.count('START_ERROR_OK'), 2)

    def test_readiness_is_non_consuming_and_timers_keep_due_interval(self):
        with tempfile.TemporaryDirectory() as d:
            request = self.process(d, 'sleep .05; printf job')
            self.run_guest(f'''!(let* (((io:pending $job) (io:submit {request}))
        ((io:pending $timer) (io:submit (timer:after 1)))
        ((io:ready ($timer)) (io:wait-ready $timer 2000))
        ((io:ready ($timer)) (io:wait-ready $timer 0))
        ((io:event $job (proc:finished $_ exited 0 $_ $_)) (io:wait $job 2000))
        ((io:event $timer (timer:fired 1)) (io:poll $timer)))
   (progn (assert (== (io:wait-ready $job 0) (io:unknown $job)))
          (println! PROVIDER_OK)))\n''', d)

    def test_owned_wait_has_one_wrapper_answer(self):
        for lang in ('petta', 'he'):
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                self.run_guest('''!(let (io:pending $id) (io:submit (timer:after 10))
    (case (collapse (io:wait $id 1000))
      ((((io:event $id (timer:fired 10))) (println! PROVIDER_OK))
       ($other (println! PROVIDER_MISMATCH)))))\n''', d, lang)

    def test_cancel_exit_races_and_shutdown_reap_owned_children(self):
        with tempfile.TemporaryDirectory() as d:
            proc_import = '!(import! &self (library proc))\n'
            request = self.process(d, 'exit 0')
            code = proc_import + '\n'.join(f'''!(let (io:pending $id) (io:submit {request})
    (progn (io:cancel $id) (assert (== (io:poll $id) (io:unknown $id)))))'''
                for _ in range(200))
            # Let the owner finish its private reaping while the evaluator is
            # blocked. Its own thread's children list must then be empty.
            code += '''\n!(import! &self (library system))
!(proc:run ("/bin/sh" "-c" "sleep .2") "/" inherit 0 0)
!(println! (OWNER_PID (system:pid)))
!(println! PROVIDER_OK)
'''
            result = self.run_guest(code, d)
            self.assertIn('OWNER_PID', result.stdout)
            marker = Path(d) / 'shutdown-survivor'
            request = self.process(d,
                f"printf running; (sleep .3; touch '{marker}') & sleep 5")
            self.run_guest(f'''!(let (io:pending $id) (io:submit {request})
    (progn (io:wait-ready 30) (println! PROVIDER_OK)))\n''', d)
            time.sleep(.4)
            self.assertFalse(marker.exists())

    def test_repeated_immediate_signals_preserve_results(self):
        with tempfile.TemporaryDirectory() as d:
            request = self.process(d, 'sleep 5')
            code = '\n'.join(f'''!(let (io:pending $id) (io:submit {request})
   (progn (io:signal $id 15)
      (case (io:wait $id 2000)
         (((io:event $id (proc:finished $_ signaled 15 $_ $_)) (println! SIGNAL_OK))))))'''
                for _ in range(100))
            result = self.run_guest(code + '\n!(println! PROVIDER_OK)\n', d)
            self.assertEqual(result.stdout.count('SIGNAL_OK'), 100)

    def test_timers_do_not_inherit_http_queue_capacity(self):
        with tempfile.TemporaryDirectory() as d:
            declarations='\n'.join('!(io:submit (timer:after 0))' for _ in range(80))
            consumes='\n'.join('!(case (io:poll) (((io:event $id (timer:fired $_)) True)))' for _ in range(80))
            self.run_guest(declarations+'\n'+consumes+'\n!(assert (== (io:poll) (io:idle)))\n!(println! PROVIDER_OK)\n',d)

    def test_mixed_http_process_and_timer_wait(self):
        if os.environ.get('CETTA_TEST_HTTP') == '0':
            self.skipTest('HTTP disabled for this build')
        peer=http.server.ThreadingHTTPServer(('127.0.0.1',0),Peer)
        thread=threading.Thread(target=peer.serve_forever,daemon=True); thread.start()
        try:
            with tempfile.TemporaryDirectory() as d:
                request=self.process(d,'sleep .05; printf native')
                code=f'''!(let* (((io:pending $http) (io:submit (http:request "GET" "http://127.0.0.1:{peer.server_port}/" () "" 2000 1000)))
                  ((io:pending $process) (io:submit {request}))
                  ((io:pending $timer) (io:submit (timer:after 10))))
                  (progn
                    (case (io:wait $process 2000) (((io:event $process (proc:finished $pid exited 0 $out $err)) True)))
                    (case (io:poll $timer) (((io:event $timer (timer:fired $_)) True)))
                    (case (io:wait $http 2000) (((io:event $http (http:response 200 "http finished")) (println! PROVIDER_OK))))))\n'''
                self.run_guest(code,d)
        finally: peer.shutdown();peer.server_close()

    def test_large_tail_and_native_clock(self):
        for lang in ('petta','he'):
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                log=Path(d)/'large.log'
                with log.open('wb') as file:
                    file.truncate(64*1024*1024)
                    file.write(b'')
                    file.seek(-6,2);file.write(b'\xe2\x82\xacEND')
                code='!(import! &self '+('(library system)' if lang=='petta' else 'system')+')\n'
                code+=f'''!(case (fs:tail-text {json.dumps(str(log))} 6)
                  (((fs:tail $size $cut $text)
                    (progn (assert (== $size 67108864)) (assert $cut)
                           (assert (== $text "€END")) (println! TAIL_OK)))
                   ($other (println! PROVIDER_MISMATCH))))
!(assert (== (system:format-time "%Y-%m-%d %H:%M:%S" 0 "utc") "1970-01-01 00:00:00"))
!(assert (== (system:format-time "" 0 "utc") ""))
!(assert (> (system:wall-time) 1700000000))
!(assert (system:pid-alive? (system:pid)))
!(println! PROVIDER_OK)
'''
                result=self.run_guest(code,d,lang)
                self.assertIn('TAIL_OK',result.stdout)

if __name__=='__main__': unittest.main()
