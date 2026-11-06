#!/usr/bin/env python3
"""Exercise the loaded provider's string reader, including long escaped bodies.

The payload is deliberately a string rather than a language component. The
provider must finish decoding and return its ordinary admission diagnostic;
it must not crash while reading a large literal before that diagnostic.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('provider', type=Path)
    args = parser.parse_args()
    path = args.provider.resolve(strict=True)
    call_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p,
        ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t))

    class Provider(ctypes.Structure):
        _fields_ = [('abi', ctypes.c_uint32), ('name', ctypes.c_char_p),
                    ('call', call_type), ('release', ctypes.CFUNCTYPE(None, ctypes.c_void_p))]

    library = ctypes.CDLL(str(path))
    library.cetta_source_compiler_v1.restype = ctypes.POINTER(Provider)
    provider = library.cetta_source_compiler_v1().contents
    assert provider.abi == 1
    cases = []
    for length in (32, 100_000, 3_000_000):
        body = b'"' + b'a' * length + b'\\q\\n\\u03bb"'
        cases.append(([body, b'LNil', b'LNil', b'LNil', b'LNil'], False))
    for body in (b'True', b'False', b'(Record (Field "flags" (List True False "True")))'):
        cases.append(([body] * 5, True))
    for values, decoded_values in cases:
        arguments = (ctypes.c_char_p * len(values))(*values)
        lengths = (ctypes.c_size_t * len(values))(*(len(v) for v in values))
        response, size = ctypes.c_void_p(), ctypes.c_size_t()
        assert provider.call(b'admit', arguments, lengths, len(values),
                             ctypes.byref(response), ctypes.byref(size)) == 1
        try:
            result = json.loads(ctypes.string_at(response, size.value))
            assert result.get('ok') is False and isinstance(result.get('message'), str), result
            if decoded_values:
                assert result['message'] == 'source, core, parser plan or execution does not match this compiler', result
        finally:
            provider.release(response)
    runtime = path.parent / 'libcetta-lean-providers.so'
    print(json.dumps({'provider_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'runtime_sha256': hashlib.sha256(runtime.read_bytes()).hexdigest()
                                        if runtime.is_file() else None,
                      'escaped_body_lengths': [32, 100_000, 3_000_000],
                      'boolean_payload_controls': 3, 'passed': True}))


if __name__ == '__main__':
    main()
