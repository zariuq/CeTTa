import gc
import math
import subprocess
import sys
import unittest
import weakref
import threading
import types
from pathlib import Path

from cetta import Engine


class PythonObjectsTests(unittest.TestCase):
    def test_preparsed_python_call_activates_with_library_import(self):
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                call = e.parse('(py:call (py:get (py:import "math") "sqrt") 81)')
                e.library('py')
                self.assertEqual(e.eval(call).one().to_python(), 9.0)

    def test_shallow_tuple_booleans_use_the_dialect_values(self):
        for language in ('petta', 'he'):
            with Engine(language=language) as e:
                e.library('py')
                truth, false = ('true', 'false') if language == 'petta' else ('True', 'False')
                source = f'(let $xs (py:to-tuple (py:list True False)) (let $tail (cdr-atom $xs) (and (== (car-atom $xs) {truth}) (== (car-atom $tail) {false}))))'
                self.assertTrue(e.eval(source).one().as_bool())
                result = e.eval(f'(let ({truth} {false}) (py:to-tuple (py:list True False)) matched)').one()
                self.assertEqual(str(result), 'matched')

    def test_boolean_round_trips_and_scalar_bindings(self):
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                identity = e.pyobject(lambda value: value)
                inspect = e.pyobject(lambda value: (type(value).__name__, value))
                for value in (False, True):
                    self.assertIs(e.call(identity, value).one().as_bool(), value)
                    self.assertEqual(e.call(inspect, value).one().to_python(), ('bool', value))
                    keyword = py.kwargs(('value', value)).one()
                    self.assertEqual(e.call(inspect, keyword).one().to_python(), ('bool', value))
                nested = e.call(identity, [False, True, 'false', 'true']).one().to_python()
                self.assertEqual(nested, [False, True, 'false', 'true'])
                e.run('!(bind! &scalar (py:call (py:get (py:import "math") "sqrt") 4))')
                self.assertEqual(e.eval('(py:call (py:get (py:import "builtins") "repr") &scalar)').one().to_python(), '2.0')
                false = 'false' if language == 'petta' else 'False'
                self.assertEqual(e.eval(f'(py:call (py:get (py:import "builtins") "repr") {false})').one().to_python(), 'False')
                self.assertEqual(e.eval('(py:call (py:get (py:import "builtins") "repr") (== 1 2))').one().to_python(), 'False')
                self.assertEqual(e.eval(f'(py-call (builtins.repr {false}))').one().to_python(), 'False')
                self.assertEqual(e.eval('(py-call (builtins.repr "false"))').one().to_python(), "'false'")

    def test_he_errors_are_values_inside_metta(self):
        with Engine(language='he') as e:
            e.library('py')
            failure = '(py:call (py:get (py:import "math") "sqrt") -1.0)'
            with self.assertRaises(ValueError): e.eval(failure)
            error = e.eval(f'(collapse {failure})').one().children[0]
            self.assertEqual(str(error.children[0]), 'Error')
            self.assertEqual(str(error.children[1].children[0]), 'py:call')
            self.assertEqual(str(error.children[2].children[1]), 'ValueError')
            caught = e.eval(f'(case {failure} (((Error $call $reason) handled) ($_ missed)))').one()
            self.assertEqual(str(caught), 'handled')
            with e.query(f'(collapse {failure})') as query:
                self.assertEqual(str(next(query).children[0].children[0]), 'Error')

    def test_final_calls_preserve_older_choices_and_reject_without_replay(self):
        for language in ('petta', 'he'):
            with Engine(language=language) as e:
                py = e.library('py')
                seen = []
                def observe(value): seen.append(value); return value
                py['bind!'](e.space.as_atom(), e.symbol('observe'), e.pyobject(observe))
                self.assertEqual(e.eval('(let $x (superpose (1 2)) (let 2 (observe $x) $x))').to_python(), [2])
                self.assertEqual(seen, [1, 2])
                seen.clear()
                self.assertEqual(e.eval('(let $x (superpose (1 2)) (let $y (observe $x) (superpose ($y $y))))').to_python(), [1, 1, 2, 2])
                self.assertEqual(seen, [1, 2])

    def test_keywords_snapshot_survives_argument_conversion(self):
        from fractions import Fraction
        from unittest.mock import patch
        for language in ('petta', 'he'):
            with Engine(language=language) as e:
                py = e.library('py')
                half = Fraction(1, 2)
                argument = e.atom(half)
                keyword = py.kwargs(('initial', False)).one()
                receive = e.pyobject(lambda value, **kwargs: (value, kwargs))
                def during_conversion(*args):
                    keyword.to_python().update({f'late{i}': i for i in range(100)})
                    return half
                with patch('fractions.Fraction', during_conversion):
                    value, kwargs = e.call(receive, argument, keyword).one().to_python()
                self.assertIs(value, half)
                self.assertEqual(kwargs, {'initial': False})
                self.assertEqual(len(keyword.to_python()), 101)

    def test_shared_native_fixture(self):
        support = Path(__file__).resolve().parents[2] / 'tests/support'
        sys.path.insert(0, str(support))
        try:
            for language in ('petta', 'he'):
                with self.subTest(language=language), Engine(language=language) as e:
                    self.assertEqual(e.load(support / 'python_objects.metta')[-1].to_python(), 19)
        finally:
            sys.path.remove(str(support))
    def test_library_and_cardinality(self):
        with Engine() as e:
            self.assertTrue(e.library('str')['starts-with']('abc', 'a').one().as_bool())
            self.assertEqual(e.call('+', 2, 3).one().to_python(), 5)
            e.run('(= (many) 1) (= (many) 1)')
            with self.assertRaises(ValueError): e.call('many').one()
            with self.assertRaises(ValueError): e.run('!(empty)').one()

    def test_objects_calls_and_keywords(self):
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                e.import_('(library py)')
                module = e.call('py:import', 'math').one()
                sqrt = e.call('py:get', module, 'sqrt').one()
                self.assertEqual(e.call(sqrt, 81).one().to_python(), 9.0)
                self.assertIs(module.to_python(), math)
                obj = []
                wrapped = e.pyobject(obj)
                append = e.call('py:get', wrapped, 'append').one()
                self.assertIsNone(e.call(append, 7).one().to_python())
                self.assertEqual(obj, [7])
                self.assertIs(wrapped.to_python(), obj)
                kw = e.eval('(py:kwargs ("reverse" True))').one()
                sorted_fn = e.pyobject(sorted)
                self.assertEqual(e.call(sorted_fn, [1, 3, 2], kw).one().to_python(), [3, 2, 1])
                e.run('!(py:bind! &self py-test:sqrt (py:get (py:import "math") "sqrt"))')
                self.assertEqual(e.call('py-test:sqrt', 81).one().to_python(), 9.0)

    def test_retained_none_aliases_and_subclasses(self):
        class Number(int): pass
        with Engine() as e:
            e.import_('(library py)')
            obj = []; obj.append(obj)
            wrapper = e.pyobject(obj)
            self.assertIs(wrapper.to_python(), obj)
            returned = e.call(e.pyobject(lambda: obj)).one()
            self.assertIs(returned.to_python(), obj)
            number = Number(3)
            self.assertIs(e.call(e.pyobject(lambda: number)).one().to_python(), number)
            none = e.call('py:none').one()
            self.assertIsNone(none.to_python())
            text = 'true\x00(+ 1 2)'
            self.assertEqual(e.call(e.pyobject(lambda x: x), text).one().to_python(), text)
        self.assertIs(returned.to_python(), obj)

    def test_cli(self):
        p = subprocess.run([sys.executable, '-m', 'cetta', '-e', '!(+ 20 22)'],
                           capture_output=True, text=True, timeout=30)
        self.assertEqual((p.returncode, p.stdout), (0, '42\n'), p.stderr)

    def test_cycles_and_exceptions(self):
        class Box: pass
        for language in ('petta', 'he'):
            e = Engine(language=language)
            e.library('py')
            box = Box(); box.engine = e
            box.atom = e.pyobject(box)
            original = ValueError('original')
            def fail(): raise original
            with self.assertRaises(ValueError) as caught:
                e.call(e.pyobject(fail))
            self.assertIs(caught.exception, original)
            with self.assertRaises(AttributeError):
                e.call('py:get', box.atom, 'missing')
            ref = weakref.ref(box); engine_ref = weakref.ref(e)
            del box, e
            gc.collect()
            self.assertIsNone(ref())
            self.assertIsNone(engine_ref())
        left, right = Engine(), Engine()
        first, second = Box(), Box()
        first.other = right.pyobject(second)
        second.other = left.pyobject(first)
        first_ref, second_ref = weakref.ref(first), weakref.ref(second)
        del left, right, first, second
        gc.collect()
        self.assertIsNone(first_ref())
        self.assertIsNone(second_ref())

    def test_iteration_and_cancellation(self):
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                self.assertEqual([v.to_python() for v in py.iter(e.pyobject([1, 2, 3]))], [1, 2, 3])
                events = []
                def generate():
                    try:
                        for i in range(5):
                            events.append(i)
                            yield i
                    finally:
                        events.append('closed')
                with py.iter.query(e.pyobject(generate())) as query:
                    self.assertEqual(next(query).to_python(), 0)
                    self.assertEqual(events, [0])
                self.assertEqual(events, [0, 'closed'])
                self.assertEqual([v.to_python() for v in e.query_call('py:list',
                    e.expression(e.symbol('+'), 1, 2))], [[[ '+', 1, 2 ]]])

    def test_context_managers(self):
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                events = []
                class Manager:
                    suppress = False
                    def __enter__(self):
                        events.append('enter')
                        return [10, 20, 30]
                    def __exit__(self, kind, error, trace):
                        events.append(kind)
                        return self.suppress
                manager = Manager()
                self.assertEqual(py['with'](e.pyobject(manager), e.pyobject(len)).one().to_python(), 3)
                self.assertEqual(events, ['enter', None])
                events.clear()
                original = ValueError('body error')
                def fail(value): raise original
                with self.assertRaises(ValueError) as caught:
                    py['with'](e.pyobject(manager), e.pyobject(fail))
                self.assertIs(caught.exception, original)
                self.assertEqual(events, ['enter', ValueError])
                events.clear(); manager.suppress = True
                self.assertEqual(py['with'](e.pyobject(manager), e.pyobject(fail)), [])
                self.assertEqual(events, ['enter', ValueError])
                events.clear(); manager.suppress = False
                e.run('(= (context-items $items) (py:iter $items))')
                with py['with'].query(e.pyobject(manager), e.symbol('context-items')) as query:
                    self.assertEqual(next(query).to_python(), 10)
                    self.assertEqual(events, ['enter'])
                self.assertEqual(events, ['enter', GeneratorExit])
                if language == 'petta':
                    import contextlib
                    e.run('(= (legacy-failure $value) (py-call (math.sqrt -1)))')
                    manager = e.pyobject(contextlib.suppress(ValueError))
                    self.assertEqual(py['with'](manager, e.symbol('legacy-failure')), [])
                    e.run('(= (metta-failure $value) (length a))')
                    original = e.eval('(catch (length a))').one().children[1]
                    source = e.expression(e.symbol('catch'), e.expression(e.symbol('py:with'),
                        e.pyobject(contextlib.nullcontext()), e.symbol('metta-failure')))
                    self.assertEqual(str(e.eval(source).one().children[1]), str(original))

    def test_protocols_bindings_and_effects(self):
        for language in ('petta', 'he'):
            with self.subTest(language=language), Engine(language=language) as e:
                py = e.library('py')
                class Callable:
                    def __call__(self, *args, **kwargs): return (args, kwargs)
                call = e.pyobject(Callable())
                keywords = py.kwargs(('answer', 42)).one()
                self.assertEqual(e.call(call, *range(20), keywords).one().to_python(),
                                 (tuple(range(20)), {'answer': 42}))
                with self.assertRaises(TypeError): e.call(call, keywords, 1)
                with self.assertRaises(TypeError): py.kwargs(('x', 1), ('x', 2))
                invalid_keywords = py.kwargs(('x', 1)).one()
                invalid_keywords.to_python()[3] = 'bad-key'
                with self.assertRaises(TypeError): e.call(call, invalid_keywords)
                data = ('answer', 42)
                self.assertEqual(e.call(call, data).one().to_python(), ((list(data),), {}))
                obj = types.SimpleNamespace(value=1)
                atom = e.pyobject(obj)
                py['set!'](atom, 'value', 9)
                self.assertEqual(py.get(atom, 'value').one().to_python(), 9)
                py['del!'](atom, 'value')
                with self.assertRaises(AttributeError): py.get(atom, 'value')
                values = py.list(1, 2, 3).one()
                py['set-item!'](values, 0, 8)
                py['del-item!'](values, 1)
                self.assertEqual(py.item(values, py.slice(0, 2).one()).one().to_python(), [8, 3])
                self.assertTrue(py['is'](values, values).one().as_bool())
                self.assertTrue(py.eq(values, py.list(8, 3).one()).one().as_bool())
                self.assertFalse(py['is'](values, py.list(8, 3).one()).one().as_bool())
                nested = [values.to_python()]
                shallow = py['to-tuple'](e.pyobject(nested)).one().to_python()
                self.assertIs(shallow[0], nested[0])
                self.assertEqual(e.call(e.pyobject(lambda x: x), 2**200).one().to_python(), 2**200)
                with self.assertRaises(UnicodeEncodeError):
                    e.call(e.pyobject(lambda: '\udcff'))
                self.assertEqual(e.eval('(let $kw (py:kwargs ("ndigits" 2)) '
                    '(py-call (builtins.round 1.234 $kw)))').one().to_python(), 1.23)
                self.assertIs(type(e.eval('(py-call (builtins.object))').one().to_python()), object)
                if language == 'petta':
                    self.assertTrue(str(e.eval('(catch (py:get (py:none) "missing"))').one()).startswith('(Error '))
                owner = types.SimpleNamespace(f=lambda *xs: sum(xs))
                captured = py.get(e.pyobject(owner), 'f').one()
                self_space = e.space.as_atom()
                py['bind!'](self_space, e.symbol('captured'), captured)
                py['bind!'](self_space, e.symbol('captured'), captured)
                owner.f = lambda *xs: -1
                self.assertEqual(e.call('captured', 1, 2, 3).one().to_python(), 6)
                py['bind!'](self_space, e.symbol('append-value'), py.get(values, 'append').one())
                py['bind!'](self_space, e.symbol('append-value'), py.get(values, 'append').one())
                e.call('append-value', 9)
                self.assertEqual(values.to_python(), [8, 3, 9])
                with self.assertRaises(ValueError):
                    py['bind!'](self_space, e.symbol('captured'), e.pyobject(owner.f))
                with self.assertRaises(ValueError): py['bind!'](self_space, e.symbol('+'), captured)
                e.run('(= (authored $x) $x)')
                with self.assertRaises(ValueError): py['bind!'](self_space, e.symbol('authored'), captured)
                self.assertEqual(e.call('authored', 9).one().to_python(), 9)
                child = e.new_space()
                py['bind!'](child.as_atom(), e.symbol('private-call'), e.pyobject(abs))
                self.assertEqual(str(e.eval('(private-call -4)').one()), '(private-call -4)')
                self.assertEqual(e.call('eval', e.parse('(private-call -4)'), child.as_atom()).one().to_python(), 4)
                late = e.parse('(late-python-call -9)')
                self.assertEqual(str(e.eval(late).one()), '(late-python-call -9)')
                py['bind!'](self_space, e.symbol('late-python-call'), e.pyobject(abs))
                self.assertEqual(e.eval(late).one().to_python(), 9)
                events = []
                def mark(value): events.append(value); return value
                py['bind!'](self_space, e.symbol('mark'), e.pyobject(mark))
                self.assertEqual(e.eval('(mark (superpose (7 8)))').to_python(), [7, 8])
                self.assertEqual(events, [7, 8])
                self.assertEqual(e.call(e.pyobject(lambda: e.call('+', 3, 4).one().to_python())).one().to_python(), 7)

    def test_busy_during_python_call(self):
        from cetta import CettaError
        entered, finish = threading.Event(), threading.Event()
        with Engine() as e:
            e.library('py')
            def wait(): entered.set(); finish.wait(10); return 42
            function = e.pyobject(wait)
            result = []
            worker = threading.Thread(target=lambda: result.extend(e.call(function).to_python()))
            worker.start()
            try:
                self.assertTrue(entered.wait(10))
                with self.assertRaises(CettaError) as caught: e.call('+', 1, 2)
                self.assertEqual(caught.exception.status, 'busy')
            finally:
                finish.set(); worker.join(10)
            self.assertFalse(worker.is_alive())
            self.assertEqual(result, [42])

    def test_opaque_wrapping_and_captured_lookup(self):
        class Payload:
            def __iter__(self): raise AssertionError('payload traversed')
            def __len__(self): raise AssertionError('payload measured')
            def __repr__(self): raise AssertionError('payload rendered')
        class Source:
            reads = 0
            @property
            def function(self):
                self.reads += 1
                return abs
        with Engine() as e:
            py = e.library('py')
            payload = Payload()
            self.assertIs(e.pyobject(payload).to_python(), payload)
            source = Source()
            selected = py.get(e.pyobject(source), 'function').one()
            py['bind!'](e.space.as_atom(), e.symbol('measured'), selected)
            for i in range(10): self.assertEqual(e.call('measured', -i).one().to_python(), i)
            self.assertEqual(source.reads, 1)

    def test_object_memory_refusal_and_recovery(self):
        from cetta import CettaError
        with Engine(memory_limit=16 * 1024**2) as e:
            self.assertEqual(e.call('+', 1, 2).one().to_python(), 3)
            retained = []
            with self.assertRaises((CettaError, MemoryError)):
                for _ in range(10000): retained.append(e.pyobject(object()))
            retained.clear()
            gc.collect()
            self.assertEqual(e.call('+', 20, 22).one().to_python(), 42)

    def test_compatibility_module_cycles(self):
        path = Path(__file__).resolve().parents[2] / 'tests/support/python_interop_exports.py'
        e = Engine(language='he')
        e.import_(path)
        self.assertEqual(e.run('! hosted-export').one().to_python(), 7)
        modules = {module for module in tuple(sys.modules.values())
                   if getattr(module, '__file__', None) == str(path)}
        self.assertTrue(modules)
        refs = [weakref.ref(e)] + [weakref.ref(module) for module in modules]
        for module in modules: module.owner = e
        for name, module in tuple(sys.modules.items()):
            if module in modules: del sys.modules[name]
        del name, module, modules, e
        gc.collect()
        self.assertTrue(all(ref() is None for ref in refs))

    def test_python_callee_can_cancel_its_query_or_engine(self):
        from cetta import CettaError
        for language in ('petta', 'he'):
            for close_engine in (False, True):
                with Engine(language=language) as e:
                    e.library('py')
                    events = []
                    query = None
                    def stop():
                        events.append('entered')
                        e.close() if close_engine else query.close()
                        return 7
                    query = e.query_call(e.pyobject(stop))
                    with self.assertRaises(CettaError) as caught: next(query)
                    self.assertIn(caught.exception.status, ('cancelled', 'engine-closed'))
                    self.assertEqual(events, ['entered'])
                    query.close()

    def test_cleanup_chains_and_host_modules(self):
        for language in ('petta', 'he'):
            with Engine(language=language) as e:
                py = e.library('py')
                original, cleanup = ValueError('body'), RuntimeError('cleanup')
                events = []
                class Manager:
                    def __enter__(self): return None
                    def __exit__(self, kind, value, trace):
                        events.append(kind)
                        raise cleanup
                def fail(value): raise original
                with self.assertRaises(RuntimeError) as caught:
                    py['with'](e.pyobject(Manager()), e.pyobject(fail))
                self.assertIs(caught.exception, cleanup)
                self.assertIs(cleanup.__context__, original)
                self.assertEqual(events, [ValueError])
                class Stream:
                    def __iter__(self): return self
                    def __next__(self): return 1
                    def close(self): raise cleanup
                query = py.iter.query(e.pyobject(Stream()))
                self.assertEqual(next(query).to_python(), 1)
                with self.assertRaises(RuntimeError) as caught: query.close()
                self.assertIs(caught.exception, cleanup)
        source = '''
import sys, types
sentinel = types.ModuleType('hyperon')
sys.modules['hyperon'] = sentinel
bridge_sentinel = types.ModuleType('cetta_bridge')
sys.modules['cetta_bridge'] = bridge_sentinel
from cetta import Engine
with Engine() as e:
    e.library('py')
    assert e.eval('(py:call (py:get (py:import "math") "sqrt") 81)').one().to_python() == 9.0
    assert e.eval('(py-call (math.sqrt 81))').one().to_python() == 9.0
assert sys.modules['hyperon'] is sentinel
assert sys.modules['cetta_bridge'] is bridge_sentinel
'''
        subprocess.run([sys.executable, '-c', source], check=True, capture_output=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
