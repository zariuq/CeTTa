"""Optional package workflows through the same object protocol in both dialects."""
import importlib.util
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading
import unittest
from unittest.mock import patch

from cetta import Engine


def available(name):
    return importlib.util.find_spec(name) is not None


class PackageTests(unittest.TestCase):
    @unittest.skipUnless(available('numpy'), 'NumPy is not installed')
    def test_numpy_and_cetta_tensor_library(self):
        import numpy as np
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                linalg = py['import']('numpy.linalg').one()
                norm = py.get(linalg, 'norm').one()
                data = np.array([3.0, 4.0])
                wrapped = e.pyobject(data)
                self.assertIs(wrapped.to_python(), data)
                self.assertEqual(e.call(norm, wrapped).one().to_python(), 5.0)
                self.assertEqual(e.call(norm, wrapped, py.kwargs(('ord', 1)).one()).one().to_python(), 7.0)
                tensor = e.library('tensor')
                self.assertEqual(tensor.sum(e.tensor(np.array([1.0, 2.0, 3.0]))).one().to_python(), 6.0)

    @unittest.skipUnless(available('pandas'), 'pandas is not installed')
    def test_pandas(self):
        import pandas as pd
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                constructor = py.get(py['import']('pandas').one(), 'DataFrame').one()
                frame = e.call(constructor, e.pyobject({'group': ['a', 'a', 'b'], 'value': [1, 2, 4]})).one()
                grouped = e.call(py.get(frame, 'groupby').one(), 'group').one()
                sums = e.call(py.get(grouped, 'sum').one()).one().to_python()
                self.assertIsInstance(sums, pd.DataFrame)
                self.assertEqual(sums['value'].to_dict(), {'a': 3, 'b': 4})

    @unittest.skipUnless(available('sympy'), 'SymPy is not installed')
    def test_sympy(self):
        import sympy as sp
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                module = py['import']('sympy').one()
                expression = e.call(py.get(module, 'sympify').one(), '(x + 1)**2').one()
                expanded = e.call(py.get(module, 'expand').one(), expression).one()
                self.assertEqual(expanded.to_python(), sp.Symbol('x')**2 + 2*sp.Symbol('x') + 1)

    @unittest.skipUnless(available('httpx'), 'HTTPX is not installed')
    def test_httpx_stream_and_early_close(self):
        import httpx
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                data = b'one\ntwo\nthree\n'
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            def log_message(self, *args): pass
        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        serving = threading.Thread(target=server.serve_forever, daemon=True)
        serving.start()
        url = f'http://127.0.0.1:{server.server_port}/stream'
        try:
            for language in ('petta', 'he'):
                with self.subTest(language=language), Engine(language=language) as e:
                    py = e.library('py')
                    seen = []
                    def remember(response): seen.append(response); return response
                    py['bind!'](e.space.as_atom(), e.symbol('remember-response'), e.pyobject(remember))
                    e.run('''(= (response-lines $response)
                        (let $r (remember-response $response)
                            (py:iter (py:call (py:get $r "iter_lines")))))''')
                    module = py['import']('httpx').one()
                    with patch.dict(os.environ, {'HTTP_PROXY': 'http://127.0.0.1:1',
                            'HTTPS_PROXY': 'http://127.0.0.1:1', 'ALL_PROXY': 'http://127.0.0.1:1',
                            'NO_PROXY': '', 'no_proxy': ''}):
                        stream = e.call(py.get(module, 'stream').one(), 'GET', url,
                                        py.kwargs(('trust_env', False)).one()).one()
                        with py['with'].query(stream, e.symbol('response-lines')) as query:
                            self.assertEqual(next(query).to_python(), 'one')
                            self.assertFalse(seen[-1].is_closed)
                    self.assertTrue(seen[-1].is_closed)
                    with httpx.stream('GET', url, trust_env=False) as response:
                        self.assertEqual(py.iter(e.pyobject(response.iter_lines())).to_python(),
                                         ['one', 'two', 'three'])
        finally:
            server.shutdown(); server.server_close(); serving.join(5)

    @unittest.skipUnless(available('torch'), 'optional CPU PyTorch is not installed')
    def test_torch_cpu(self):
        import torch
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                module = py['import']('torch').one()
                tensor = e.call(py.get(module, 'tensor').one(), [1.0, 2.0, 3.0]).one()
                total = e.call(py.get(module, 'sum').one(), tensor).one()
                self.assertIsInstance(total.to_python(), torch.Tensor)
                self.assertEqual(e.call(py.get(total, 'item').one()).one().to_python(), 6.0)
