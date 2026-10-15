"""Native SIWC qualification. Synthetic IdP and generated test keys only.

SIWC_DRIVER must name tests/siwc_driver.c linked with CETTA_SIWC_TEST.
Production builds have no arbitrary-issuer environment switch.
"""
import base64
import hashlib
import http.client
import http.server
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request


def b64(value):
    return base64.urlsafe_b64encode(value).decode().rstrip('=')


class Peer(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass

    def reply(self, value, status=200):
        body = json.dumps(value).encode()
        self.send_response(status); self.send_header('Content-Length', str(len(body))); self.end_headers()
        try: self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError): pass

    def do_GET(self):
        p = self.server.owner
        if self.path == '/v1/models':
            p.api_requests.append((self.path, dict(self.headers), None))
            if p.options.get('reflect_token'):
                self.reply({'echo': self.headers.get('Authorization', '')}); return
            self.reply({'models': [dict(slug='hidden-fixture', display_name='Hidden', visibility='hide'),
                dict(slug='fixture-model', display_name='Fixture model', visibility='list'),
                dict(slug='fixture-second', display_name='Second model', visibility='list')]})
        elif self.path == '/.well-known/openid-configuration':
            self.reply(dict(issuer=p.issuer, authorization_endpoint=p.issuer+'/authorize',
                token_endpoint=p.issuer+'/token', jwks_uri=p.issuer+'/jwks', revocation_endpoint=p.issuer+'/revoke'))
        elif self.path == '/jwks':
            p.jwks_count += 1
            key = p.jwk[p.options.get('key', 0)]
            self.reply({'keys': [key, key] if p.options.get('duplicate_jwk') else [key]})
        elif self.path.startswith('/authorize?'):
            query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
            p.authorizations.append(query)
            client = query['client_id'][0]
            if client == 'dynamic_agent_client': client = 'oaiapp_fixture_' + str(len(p.clients))
            subject = p.options.get('subject', p.clients.get(client, 'fixture-subject'))
            p.clients[client] = subject
            code = 'code_' + str(len(p.codes))
            p.codes[code] = dict(client=client, subject=subject, nonce=query['nonce'][0],
                challenge=query['code_challenge'][0], redirect=query['redirect_uri'][0])
            callback = dict(code=code, state=query['state'][0])
            if not p.options.get('omit_client'): callback['client_id'] = p.options.get('returned_client', client)
            url = query['redirect_uri'][0] + '?' + urllib.parse.urlencode(callback)
            self.send_response(302); self.send_header('Location', url); self.send_header('Content-Length', '0'); self.end_headers()
        else: self.reply({}, 404)

    def do_POST(self):
        p = self.server.owner
        if self.path == '/v1/responses':
            request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            p.api_requests.append((self.path, dict(self.headers), request))
            final = dict(id='resp_fixture', status='completed', output=[
                dict(type='reasoning', id='reason_fixture', summary=[], encrypted_content='opaque-fixture'),
                dict(type='message', role='assistant', status='completed', content=[dict(type='output_text', text='(nop)', annotations=[])])],
                usage=dict(input_tokens=10, output_tokens=2))
            events = [dict(type='response.output_text.delta', delta='(nop)')]
            if p.options.get('quota_failure'):
                events.append(dict(type='response.failed', response=dict(status='failed',
                    error=dict(code='subscription_sharing_usage_limit_exceeded', message='fixture quota'))))
            elif not p.options.get('truncate_stream'):
                events.append(dict(type='response.completed', response=final))
            data = ''.join('event: '+event['type']+'\ndata: '+json.dumps(event)+'\n\n' for event in events).encode()
            self.send_response(200); self.send_header('Content-Type', 'text/event-stream'); self.end_headers()
            try:
                for start in range(0, len(data), 19): self.wfile.write(data[start:start+19]); self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError): pass
            return
        form = urllib.parse.parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
        if self.path == '/revoke':
            p.revocations.append(form)
            if p.options.get('revoke_failure'): self.reply({}, 503)
            else:
                self.send_response(200); self.send_header('Content-Length', '0'); self.end_headers()
            return
        if self.path != '/token': self.reply({}, 404); return
        p.exchanges.append(form)
        if form['grant_type'] == ['authorization_code']:
            info = p.codes.get(form.get('code', [''])[0])
            valid = info and form.get('client_id') == [info['client']] and form.get('redirect_uri') == [info['redirect']]
            verifier = form.get('code_verifier', [''])[0]
            valid = valid and b64(hashlib.sha256(verifier.encode()).digest()) == info['challenge']
            if not valid: self.reply({'error': 'invalid_grant'}, 400); return
        else:
            p.refresh_count += 1
            time.sleep(p.options.get('refresh_delay', 0))
            if p.options.get('drop_refresh'):
                self.close_connection = True; return
            if p.options.get('invalid_refresh'):
                self.reply({'error': 'invalid_grant', 'error_description': 'fixture-refresh secret must not escape'}, 400); return
            info = p.refresh_tokens.pop(form.get('refresh_token', [''])[0], None)
            if not info or form.get('client_id') != [info['client']]:
                self.reply({'error': 'invalid_grant'}, 400); return
        if form.get('resource') != ['https://api.openai.com/v1']:
            self.reply({'error': 'invalid_request'}, 400); return
        index = len(p.exchanges)
        claims = p.claims(info['client'], info['nonce'], info['subject'])
        claims.update(p.options.get('claims', {}))
        if form['grant_type'] == ['refresh_token']: claims.pop('nonce', None)
        token = dict(access_token='fixture-access-'+str(index), refresh_token='fixture-refresh-'+str(index),
            id_token=p.sign(claims, p.options.get('key', 0)), token_type='Bearer', expires_in=3600,
            scope=p.options.get('scope', 'openid offline_access chatgpt.tokens.use.direct'),
            earliest_refresh_at={'opaque_fixture': True})
        p.refresh_tokens[token['refresh_token']] = info
        self.reply(token)


@unittest.skipUnless(os.environ.get('SIWC_DRIVER'), 'set SIWC_DRIVER to the native synthetic-provider driver')
class NativeSiwcTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keys_temp = tempfile.TemporaryDirectory()
        cls.key_files = []
        cls.jwk = []
        for i in range(2):
            key = Path(cls.keys_temp.name) / ('fixture-%d.pem' % i)
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048',
                            '-pkeyopt', 'rsa_keygen_pubexp:65537', '-out', str(key)], check=True, capture_output=True)
            modulus = subprocess.check_output(['openssl', 'rsa', '-in', str(key), '-noout', '-modulus'], text=True).strip().split('=')[1]
            cls.key_files.append(key)
            cls.jwk.append(dict(kty='RSA', kid='fixture-key-%d' % i, use='sig', alg='RS256',
                                n=b64(bytes.fromhex(modulus)), e='AQAB'))

    @classmethod
    def tearDownClass(cls): cls.keys_temp.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = Path(self.tmp.name) / 'credentials'
        self.options = {}; self.codes = {}; self.clients = {}; self.refresh_tokens = {}
        self.authorizations = []; self.exchanges = []; self.revocations = []
        self.api_requests = []
        self.refresh_count = 0; self.jwks_count = 0
        self.peer = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Peer)
        self.peer.owner = self
        self.issuer = 'http://127.0.0.1:%d' % self.peer.server_port
        threading.Thread(target=self.peer.serve_forever, daemon=True).start()
        self.env = {**os.environ, 'CETTA_SIWC_STORE': str(self.store), 'CETTA_SIWC_TEST_ISSUER': self.issuer}
        self.children = []

    def tearDown(self):
        for p in self.children:
            if p.poll() is None: p.kill()
            p.communicate()
        self.peer.shutdown(); self.peer.server_close(); self.tmp.cleanup()

    def claims(self, client='oaiapp_fixture', nonce='fixture-nonce', subject='fixture-subject'):
        now = int(time.time())
        return dict(iss=self.issuer, aud=client, sub=subject, exp=now+600, iat=now, nonce=nonce)

    def sign(self, claims, key=0, header=None):
        header = header or {'alg': 'RS256', 'kid': self.jwk[key]['kid']}
        body = (b64(json.dumps(header).encode()) + '.' + b64(json.dumps(claims).encode())).encode()
        signature = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(self.key_files[key])],
                                   input=body, capture_output=True, check=True).stdout
        return body.decode() + '.' + b64(signature)

    def command(self, *args):
        p = subprocess.run([os.environ['SIWC_DRIVER'], *args], env=self.env, text=True, input='',
                           capture_output=True, timeout=10)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertNotIn('fixture-access-', p.stdout + p.stderr)
        self.assertNotIn('fixture-refresh-', p.stdout + p.stderr)
        return p.stdout

    def run_operation(self, kind, identity, deadline='5000'):
        output = self.command('run', kind, identity, '0', deadline)
        return json.loads(output.splitlines()[-1])

    def start(self, identity='new', deadline='10000'):
        p = subprocess.Popen([os.environ['SIWC_DRIVER'], 'run', 'sign-in', identity, '0', deadline],
            env=self.env, text=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=1)
        self.children.append(p)
        selector = selectors.DefaultSelector(); selector.register(p.stdout, selectors.EVENT_READ)
        due = time.monotonic()+5
        try:
            while time.monotonic() < due:
                if not selector.select(.1): continue
                line = p.stdout.readline()
                if not line: break
                value = json.loads(line)
                if 'authorization_url' in value: return p, value
        finally: selector.close()
        self.fail('sign-in did not expose browser URL: '+str(p.poll()))

    def callback_url(self, status):
        url = urllib.parse.urlsplit(status['authorization_url'])
        peer = http.client.HTTPConnection(url.hostname, url.port)
        peer.request('GET', url.path+'?'+url.query)
        response = peer.getresponse(); self.assertEqual(response.status, 302)
        target = response.getheader('Location'); response.read(); peer.close()
        return target

    def complete(self, p, target):
        try:
            with urllib.request.urlopen(target, timeout=5) as response: response.read()
        except urllib.error.HTTPError as error: error.read()
        output, errors = p.communicate(timeout=5)
        self.assertEqual(p.returncode, 0, errors)
        self.assertNotIn('fixture-access-', output+errors); self.assertNotIn('fixture-refresh-', output+errors)
        return json.loads(output.splitlines()[-1])

    def login(self):
        p, status = self.start()
        result = self.complete(p, self.callback_url(status))
        self.assertEqual(result['status'], 'ok', result)
        return result['registration_id']

    def record(self, identity): return self.store / (identity+'.json')

    def expire(self, identity):
        path = self.record(identity)
        record = json.loads(path.read_text()); record['access_expires_at'] = int(time.time())-100
        path.write_text(json.dumps(record)); return record

    def test_pkce_vector_and_randomness(self):
        a = self.command('crypto').splitlines(); b = self.command('crypto').splitlines()
        self.assertEqual(a[0], 'E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM')
        self.assertEqual(len(a[1]), 43); self.assertNotEqual(a[1], b[1])

    def test_signed_claims_algorithm_audience_times_and_key_identity(self):
        now = int(time.time()); baseline = self.claims()
        cases = [({}, None, 'ok'), ({'iss': 'other'}, None, 'issuer-mismatch'),
            ({'aud': 'other'}, None, 'audience-mismatch'), ({'nonce': 'other'}, None, 'nonce-mismatch'),
            ({'exp': now-20}, None, 'token-expired'), ({'nbf': now+30}, None, 'jwt-time-invalid'),
            ({'iat': 'bad'}, None, 'jwt-time-invalid'),
            ({'aud': ['other', 'oaiapp_fixture'], 'azp': 'oaiapp_fixture'}, None, 'ok'),
            ({'aud': ['other', 'oaiapp_fixture']}, None, 'audience-mismatch'),
            ({}, {'alg': 'HS256', 'kid': self.jwk[0]['kid']}, 'jwt-alg-rejected'),
            ({}, {'alg': 'none', 'kid': self.jwk[0]['kid']}, 'jwt-alg-rejected'),
            ({}, {'alg': 'RS256', 'kid': 'unknown'}, 'jwks-key-not-found')]
        jwt_path = Path(self.tmp.name)/'token'; keys = Path(self.tmp.name)/'jwks'
        keys.write_text(json.dumps({'keys': [self.jwk[0]]}))
        for change, header, expected in cases:
            with self.subTest(change=change, header=header):
                jwt_path.write_text(self.sign({**baseline, **change}, header=header))
                self.assertEqual(self.command('verify', str(jwt_path), str(keys), self.issuer,
                    'oaiapp_fixture', 'fixture-nonce', str(now)).strip(), expected)
        jwt_path.write_text(self.sign(baseline, key=1, header={'alg': 'RS256', 'kid': self.jwk[0]['kid']}))
        self.assertEqual(self.command('verify', str(jwt_path), str(keys), self.issuer,
            'oaiapp_fixture', 'fixture-nonce', str(now)).strip(), 'jwt-signature-invalid')

    def test_initial_registration_restart_scope_and_credential_isolation(self):
        identity = self.login()
        record = json.loads(self.record(identity).read_text())
        self.assertNotEqual(record['client_id'], 'dynamic_agent_client')
        self.assertEqual(self.exchanges[0]['client_id'], [record['client_id']])
        self.assertEqual(self.authorizations[0]['agent_name_hint'], ['godelclaw'])
        self.assertEqual(self.record(identity).stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.store.stat().st_mode & 0o777, 0o700)
        self.assertEqual(self.run_operation('ensure', identity)['status'], 'ok')
        self.assertEqual(len(self.exchanges), 1)
        accounts = self.run_operation('accounts', '')['value']
        self.assertEqual(accounts[0]['registration_id'], identity)
        self.assertNotIn('access_token', accounts[0]); self.assertNotIn('refresh_token', accounts[0])
        self.assertNotIn('id_token', accounts[0]); self.assertNotIn('sub', accounts[0])

    def test_wrong_state_prefetch_and_duplicate_state_do_not_consume_attempt(self):
        p, status = self.start(); target = self.callback_url(status)
        parts = urllib.parse.urlsplit(target); query = urllib.parse.parse_qs(parts.query)
        wrong = target.replace(query['state'][0], 'wrong')
        for url in [parts._replace(query='').geturl(), wrong, target+'&state=wrong',
                    parts._replace(query='state='+query['state'][0]).geturl(),
                    target.replace('/auth/callback', '/favicon.ico')]:
            with self.assertRaises(urllib.error.HTTPError): urllib.request.urlopen(url, timeout=2)
            self.assertIsNone(p.poll())
        self.assertEqual(self.complete(p, target)['status'], 'ok')
        self.assertEqual(len(self.exchanges), 1)

    def test_authorization_denial_and_ambiguous_callback_never_exchange(self):
        for suffix, expected in [('error=access_denied', 'authorization-denied'),
                                  ('code=one&code=two', 'callback-protocol-error')]:
            p, status = self.start(); url = urllib.parse.urlsplit(status['authorization_url'])
            query = urllib.parse.parse_qs(url.query)
            target = query['redirect_uri'][0]+'?state='+query['state'][0]+'&'+suffix
            self.assertEqual(self.complete(p, target)['code'], expected)
        self.assertEqual(self.exchanges, [])

    def test_missing_issued_client_scope_nonce_and_ambiguous_jwks(self):
        for options, expected in [({'omit_client': True}, 'client-id-missing'),
            ({'scope': 'openid'}, 'scope-missing'), ({'claims': {'nonce': 'other'}}, 'nonce-mismatch'),
            ({'duplicate_jwk': True}, 'jwks-ambiguous-key')]:
            with self.subTest(options=options):
                self.options = options; p, status = self.start()
                result = self.complete(p, self.callback_url(status))
                self.assertEqual(result['code'], expected)
                if self.record(status['registration_id']).exists():
                    self.assertNotIn('access_token', json.loads(self.record(status['registration_id']).read_text()))

    def test_returning_registration_identity_and_stable_host_id(self):
        identity = self.login(); old = json.loads(self.record(identity).read_text())
        self.options = {'omit_client': True}
        p, status = self.start(identity)
        self.assertEqual(self.complete(p, self.callback_url(status))['status'], 'ok')
        self.assertEqual(self.authorizations[0]['ext_agent_host_id'], self.authorizations[1]['ext_agent_host_id'])
        self.assertEqual(self.authorizations[1]['client_id'], [old['client_id']])
        self.assertNotIn('agent_name_hint', self.authorizations[1])
        self.options = {'subject': 'different-subject'}
        p, status = self.start(identity)
        before = self.record(identity).read_text()
        self.assertEqual(self.complete(p, self.callback_url(status))['code'], 'identity-mismatch')
        self.assertEqual(self.record(identity).read_text(), before)

    def test_rotating_refresh_is_serialized_across_processes_and_survives_restart(self):
        identity = self.login(); old = self.expire(identity); self.options = {'refresh_delay': .15, 'key': 1}
        children = [subprocess.Popen([os.environ['SIWC_DRIVER'], 'run', 'ensure', identity, '0', '5000'],
            env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(5)]
        self.children.extend(children)
        for p in children:
            output, errors = p.communicate(timeout=8)
            self.assertEqual(p.returncode, 0, errors); self.assertEqual(json.loads(output.splitlines()[-1])['status'], 'ok')
        self.assertEqual(self.refresh_count, 1)

        after = json.loads(self.record(identity).read_text())
        self.assertNotEqual(old['refresh_token'], after['refresh_token'])
        self.assertEqual(after['generation'], old['generation']+1)
        self.assertEqual(self.run_operation('ensure', identity)['status'], 'ok')
        self.assertEqual(self.refresh_count, 1)

    def test_rotating_refresh_is_serialized_between_threads(self):
        identity = self.login(); self.expire(identity); self.options = {'refresh_delay': .1}
        result = self.command('pair', 'ensure', identity, '0', '5000')
        self.assertEqual([json.loads(row)['status'] for row in result.splitlines()], ['ok', 'ok'])
        self.assertEqual(self.refresh_count, 1)

    def test_refresh_crashes_keep_complete_records_and_honest_renewal_state(self):
        for point in ('partial-temp', 'before-rename', 'after-commit'):
            with self.subTest(point=point):
                identity = self.login(); old = self.expire(identity)
                env = {**self.env, 'CETTA_SIWC_TEST_STORE_FAULT': point}
                result = subprocess.run([os.environ['SIWC_DRIVER'], 'run', 'ensure', identity, '0', '5000'],
                    env=env, text=True, capture_output=True, timeout=8)
                self.assertEqual(result.returncode, 86, result.stderr)
                record = json.loads(self.record(identity).read_text())
                self.assertEqual(record['client_id'], old['client_id'])
                if point == 'after-commit':
                    self.assertEqual(record['generation'], old['generation']+1)
                    self.assertEqual(self.run_operation('ensure', identity)['status'], 'ok')
                else:
                    self.assertEqual(record['refresh_token'], old['refresh_token'])
                    self.assertEqual(self.run_operation('ensure', identity)['code'], 'refresh-outcome-unknown')
    def test_lost_refresh_is_reported_without_replaying_old_rotating_token(self):
        identity = self.login(); self.expire(identity); self.options = {'drop_refresh': True}
        self.assertEqual(self.run_operation('ensure', identity)['code'], 'refresh-outcome-unknown')
        self.assertEqual(self.run_operation('ensure', identity)['code'], 'refresh-outcome-unknown')
        self.assertEqual(self.refresh_count, 1)

    def test_invalid_grant_clears_only_session_and_signout_keeps_registration(self):
        identity = self.login(); self.expire(identity); self.options = {'invalid_refresh': True}
        self.assertEqual(self.run_operation('ensure', identity)['code'], 'invalid-grant')
        record = json.loads(self.record(identity).read_text())
        self.assertIn('client_id', record); self.assertIn('sub', record); self.assertNotIn('refresh_token', record)
        self.options = {}; p, status = self.start(identity)
        self.assertEqual(self.complete(p, self.callback_url(status))['status'], 'ok')
        self.assertEqual(self.run_operation('sign-out', identity)['status'], 'ok')
        record = json.loads(self.record(identity).read_text())
        self.assertIn('client_id', record); self.assertNotIn('id_token', record); self.assertNotIn('access_token', record)
        self.assertEqual(len(self.revocations), 1)

    def test_unconfirmed_revocation_still_clears_local_tokens(self):
        identity = self.login(); self.options = {'revoke_failure': True}
        self.assertEqual(self.run_operation('sign-out', identity)['code'], 'revocation-unconfirmed')
        self.assertNotIn('access_token', json.loads(self.record(identity).read_text()))

    def test_signout_clears_a_session_without_a_refresh_token(self):
        identity = self.login()
        path = self.record(identity); record = json.loads(path.read_text())
        del record['refresh_token']; path.write_text(json.dumps(record))
        self.assertEqual(self.run_operation('sign-out', identity)['status'], 'ok')
        record = json.loads(path.read_text())
        self.assertNotIn('access_token', record); self.assertNotIn('id_token', record)
        self.assertIn('client_id', record); self.assertEqual(self.revocations, [])

    @unittest.skipUnless(os.environ.get('SIWC_ENGINE'), 'set SIWC_ENGINE to an SIWC_TEST engine')
    def test_native_metta_browser_command_and_owned_completion(self):
        source = Path(__file__).resolve().parents[1] / 'examples/siwc_sign_in.metta'
        for lang in ('petta', 'he'):
            with self.subTest(lang=lang):
                p = subprocess.Popen([os.environ['SIWC_ENGINE'], '--lang', lang, '--quiet', str(source)],
                    env={**self.env, 'CETTA_SIWC_DEADLINE_MS': '5000'}, text=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.children.append(p)
                line = p.stdout.readline().strip()
                value = json.loads(line)
                if isinstance(value, str): value = json.loads(value)
                target = self.callback_url(value)
                with urllib.request.urlopen(target, timeout=5) as response: response.read()
                output, errors = p.communicate(timeout=10)
                self.assertEqual(p.returncode, 0, errors + output)
                self.assertNotIn('fixture-access-', output+errors)
                self.assertNotIn('fixture-refresh-', output+errors)
                result = json.loads(output.splitlines()[0])
                if isinstance(result, str): result = json.loads(result)
                self.assertEqual(result['status'], 'ok', result)

    def test_alias_scope_and_unsafe_record_permissions(self):
        identity = self.login(); alias = 'SIWC:'+identity
        self.assertEqual(self.command('bearer', alias, self.issuer+'/v1/models').strip(), 'available')
        for url in [self.issuer+'/v1/../token', self.issuer+'/v1/%2e%2e/token',
                    self.issuer+'/v1/%2foutside', self.issuer+'/v1/models#fragment', self.issuer+'/token',
                    self.issuer+'.evil/v1/models', self.issuer.replace('127.0.0.1', '127.0.0.1@evil')+'/v1/models']:
            self.assertEqual(self.command('bearer', alias, url).strip(), 'unavailable')
        self.record(identity).chmod(0o644)
        self.assertEqual(self.command('bearer', alias, self.issuer+'/v1/models').strip(), 'unavailable')

    def test_deadline_and_cancel_release_callback_listener(self):
        p, status = self.start(deadline='300')
        target = urllib.parse.parse_qs(urllib.parse.urlsplit(status['authorization_url']).query)['redirect_uri'][0]
        output, _ = p.communicate(timeout=3)
        self.assertEqual(json.loads(output.splitlines()[-1])['code'], 'deadline')
        with self.assertRaises(urllib.error.URLError): urllib.request.urlopen(target, timeout=.5)
        p, status = self.start()
        p.stdin.write('cancel\n'); p.stdin.flush()
        output, _ = p.communicate(timeout=3); self.assertIn('cancelled', output)
        target = urllib.parse.parse_qs(urllib.parse.urlsplit(status['authorization_url']).query)['redirect_uri'][0]
        with self.assertRaises(urllib.error.URLError): urllib.request.urlopen(target, timeout=.5)


if __name__ == '__main__': unittest.main()
