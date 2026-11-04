#!/usr/bin/env python3
"""Compare the native raw-region scanner/printer with a byte-search reference."""

import ctypes as c
import itertools
import re
import sys


class Region(c.Structure):
    _fields_ = [(name, c.c_size_t) for name in
                ("start", "body_start", "body_end", "end", "fence_depth", "error_offset")]


def reference(data, start):
    opener = re.match(rb'r(#+)"', data[start:])
    if not opener:
        return 0, None
    body_start = start + opener.end()
    depth = len(opener[1])
    end = data.find(b'"' + b'#' * depth, body_start)
    try:
        data[body_start:end if end >= 0 else len(data)].decode("utf-8")
    except UnicodeDecodeError as error:
        return (2 if end < 0 and error.reason == "unexpected end of data" else 3), None
    if end < 0:
        return 2, None
    return 1, (start, body_start, end, end + 1 + depth, depth)


def main(path):
    native = c.CDLL(path)
    native.cetta_raw_source_scan.argtypes = [c.c_char_p, c.c_size_t, c.c_size_t, c.POINTER(Region)]
    native.cetta_raw_source_scan.restype = c.c_int
    native.cetta_raw_source_render.argtypes = [c.c_char_p, c.c_size_t, c.c_size_t,
                                             c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    native.cetta_raw_source_render.restype = c.c_bool
    checks = 0

    def check(data, start):
        nonlocal checks
        region = Region()
        actual = native.cetta_raw_source_scan(data, len(data), start, c.byref(region))
        expected, fields = reference(data, start)
        assert actual == expected, (data, start, actual, expected)
        if fields:
            assert tuple(getattr(region, name) for name, _ in Region._fields_[:5]) == fields
        checks += 1

    for length in range(6):
        for chars in itertools.product((b'"', b'#', b'\\', b';', b'\0'), repeat=length):
            body = b''.join(chars)
            for preferred in (0, 1, 3):
                needed = c.c_size_t()
                assert native.cetta_raw_source_render(body, len(body), preferred, None, 0, c.byref(needed))
                output = c.create_string_buffer(needed.value)
                assert native.cetta_raw_source_render(body, len(body), preferred, output,
                                                     needed.value, c.byref(needed))
                rendered = output.raw
                depth = max(1, preferred)
                while b'"' + b'#' * depth in body:
                    depth += 1
                assert rendered == b'r' + b'#' * depth + b'"' + body + b'"' + b'#' * depth
                check(b'xx' + rendered + b'\xff', 2)
            for depth in (1, 2, 3):
                check(b'r' + b'#' * depth + b'"' + body, 0)
    for body in ('∀🌱', '\r\n  python("quoted")\n', 'x\x00y'):
        full = b'r#"' + body.encode() + b'"#'
        for stop in range(len(full) + 1):
            check(full[:stop], 0)
    for invalid in (b'\xc0\xaf', b'\xed\xa0\x80', b'\xf4\x90\x80\x80', b'\x80', b'\xe2x'):
        check(b'r#"' + invalid, 0)
        check(b'r#"' + invalid + b'"#', 0)
    print(f"RawSourceRegion: {checks} independent scanner comparisons; printer minimality and roundtrips pass")


if __name__ == "__main__":
    main(sys.argv[1])
