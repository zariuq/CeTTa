"""Buffered provider stream controls; optional independent differential oracle."""
import argparse
from contextlib import closing
import importlib.util
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from value_worker_client import Worker
from space_service_client import E, S


def frame(value, event='', ending='\n'):
    return (('event: ' + event + ending) if event else '') + 'data: ' + json.dumps(value, ensure_ascii=False) + ending * 2


def chat(delta, finish=None, usage=None, identity='response'):
    value = dict(id=identity, choices=[dict(index=0, delta=delta, finish_reason=finish)])
    if usage is not None:
        value['usage'] = usage
    return frame(value)


def generated(rng, count):
    for index in range(count):
        if index % 2 == 0:
            parts = [rng.choice(['a', 'a', '', 'λ', '\n', '"', '🌿', '\\']) for _ in range(rng.randrange(1, 24))]
            body = ''.join(chat({'content': part, **({'reasoning': part} if rng.randrange(3) == 0 else {})}) for part in parts)
            body += chat({}, rng.choice(['stop', 'length', 'refusal', 'content_filter']), {'prompt_tokens': index, 'completion_tokens': len(parts)})
            body += 'data: [DONE]' + rng.choice(['', '\n', '\n\n'])
            if rng.randrange(3) == 0:
                body = rng.choice(['\ufeff', ': comment\n\n']) + body
            if rng.randrange(3) == 0:
                mutation = rng.randrange(5)
                if mutation == 0: body += chat({'content': 'late'})
                elif mutation == 1: body = body.replace('[DONE]', '[TRUNCATED]')
                elif mutation == 2: body = body.replace('"index": 0', '"index": true', 1)
                elif mutation == 3: body = body.replace('"response"', '"other"', 1)
                else: body = body[:rng.randrange(len(body))]
            yield 'chat-completions', True, body, 8388608
        else:
            start = dict(id='message', role='assistant', content=[], usage={'input_tokens': index}, extra={'zero': 0})
            body = frame(dict(type='message_start', message=start), 'message_start')
            for block_index in range(rng.randrange(1, 6)):
                block = dict(type='text', text=rng.choice(['', 'prefix']))
                body += frame(dict(type='content_block_start', index=block_index, content_block=block))
                for _ in range(rng.randrange(1, 8)):
                    body += frame(dict(type='content_block_delta', index=block_index,
                                       delta=dict(type='text_delta', text=rng.choice(['a', '', 'λ', '\n', '🌿']))))
                body += frame(dict(type='content_block_stop', index=block_index))
            body += frame(dict(type='message_delta', delta={'stop_reason': 'end_turn'}, usage={'output_tokens': index}))
            body += frame(dict(type='message_stop'))
            if rng.randrange(3) == 0:
                mutation = rng.randrange(5)
                if mutation == 0: body += frame(dict(type='ping'))
                elif mutation == 1: body = body.replace('message_stop', 'ping')
                elif mutation == 2: body = body.replace('text_delta', 'thinking_delta', 1)
                elif mutation == 3: body = body.replace('"index": 0', '"index": 1.0', 1)
                else: body = body[:rng.randrange(len(body))]
            if rng.randrange(2): body = body.replace('\n', rng.choice(['\r\n', '\r']))
            yield 'anthropic', True, body, 8388608


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--fuzz', type=int, default=0)
    parser.add_argument('--seed', type=int, default=719451)
    parser.add_argument('--he-cetta', type=Path, help='also exercise the public API in HE extended')
    args = parser.parse_args()
    fixture = json.loads((ROOT / 'tests/support/fixtures/llm-stream-v1.json').read_text())
    cases = [(row['protocol'], row['stream'], row['body'], row['limit'], row['result'], row['name']) for row in fixture['cases']]
    reference = None
    if args.reference:
        spec = importlib.util.spec_from_file_location('independent_stream_reference', args.reference)
        reference = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reference)
    if args.fuzz and not reference:
        parser.error('--fuzz needs an independent --reference')
    for i, (protocol, stream, body, limit) in enumerate(generated(random.Random(args.seed), args.fuzz)):
        try:
            expected = (reference.anthropic if protocol == 'anthropic' else reference.chat)(body, limit)
        except (ValueError, TypeError, KeyError, RecursionError):
            expected = None
        cases.append((protocol, stream, body, limit, expected, 'fuzz-' + str(i)))
    started = time.monotonic()
    valid = invalid = 0
    with closing(Worker(str(args.worker.resolve()), ROOT / 'tests/support/llm_stream_probe.metta',
                        'stream-probe:call', language='petta', import_mode='relative',
                        maximum_bytes=64 * 1048576)) as worker:
        for protocol, stream, body, limit, expected, name in cases:
            actual = worker.call(E('Decode', protocol, stream, body, limit))
            if expected is None:
                assert len(actual) == 2 and actual[0] == S('llm:v1:error'), (name, actual)
                invalid += 1
            else:
                assert actual == E('llm:v1:ok', expected), (name, actual, expected)
                valid += 1
    he_cases = 0
    if args.he_cetta:
        def quoted(text):
            # The HE source reader deliberately has fewer string escapes than
            # JSON. Construct unusual control scalars through the public text
            # API rather than changing the reader or dropping those cases.
            if any(ord(c) < 32 and c not in '\b\f\n\r\t' for c in text):
                return '(str:from-codepoints (%s))' % ' '.join(str(ord(c)) for c in text)
            return json.dumps(text, ensure_ascii=False)
        forms = ['!(import! &self (library llm))']
        for index, row in enumerate(fixture['cases']):
            call = '(llm:v1:response-body %s %s %s %d)' % (
                quoted(row['protocol']), 'True' if row['stream'] else 'False',
                quoted(row['body']), row['limit'])
            expected = row['result']
            result = ('(((llm:v1:error $why) True) ($other False))' if expected is None else
                      '(((llm:v1:ok $text) (let $expected %s (== $text $expected))) ($other False))' % quoted(expected))
            forms.append('!(println! (STREAM-CONTROL-%d (case %s %s)))' % (index, call, result))
        with tempfile.TemporaryDirectory(prefix='cetta-llm-stream-') as directory:
            entry = Path(directory) / 'controls.metta'
            entry.write_text('\n'.join(forms) + '\n')
            run = subprocess.run([str(args.he_cetta.resolve()), '--lang', 'he', '--profile', 'extended',
                                  '--quiet', str(entry)], cwd=ROOT, text=True, capture_output=True, timeout=60)
            assert run.returncode == 0, run.stderr[-2000:]
            for index in range(len(fixture['cases'])):
                assert '(STREAM-CONTROL-%d True)' % index in run.stdout, (index, run.stdout[-3000:], run.stderr[-1000:])
                he_cases += 1
    print(json.dumps(dict(status='passed', cases=len(cases), valid=valid, invalid=invalid, he_cases=he_cases,
                          seed=args.seed, seconds=time.monotonic() - started)))


if __name__ == '__main__':
    main()
