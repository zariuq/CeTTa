#!/usr/bin/env python3
"""Independent codec, line and literal-search controls for native text preparation."""
import argparse
import codecs
import json
from pathlib import Path
import random
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from gslt2parse_schema_v1 import StringLiteral, Symbol, parse_sexprs


def quote(value):
    return json.dumps(value, ensure_ascii=False)


def raw(value):
    escaped = "".join(f"\\x{byte:02x}" for byte in value)
    return f"(str:unescape metta {quote(escaped)})"


def values(stdout):
    records = []
    # Unicode line separators can occur within quoted strings. Only the
    # printer's actual LF terminates a record, unlike str.splitlines().
    for line in stdout.decode("utf-8").split("\n"):
        if not line.strip() or line == "true":
            continue
        body = line[1:-1] if line.startswith("[") and line.endswith("]") else line
        # MeTTa prints byte escapes, including DEL, that are not JSON syntax.
        # Translate only actual escapes inside strings, not an escaped slash
        # followed by a literal x. The schema parser then handles the tree.
        translated = []
        inside = False
        offset = 0
        while offset < len(body):
            char = body[offset]
            if char == '"':
                inside = not inside
            if inside and char == "\\" and offset + 1 < len(body):
                if body[offset + 1] == "x" and offset + 3 < len(body):
                    translated.append("\\u00" + body[offset + 2:offset + 4])
                    offset += 4
                    continue
                translated.append(body[offset:offset + 2])
                offset += 2
                continue
            translated.append(char)
            offset += 1
        records.extend(parse_sexprs("".join(translated)))
    return [record for record in records if record != ()]


def headed(value, name):
    return isinstance(value, tuple) and value and value[0] == Symbol(name)


def decoded(value):
    assert isinstance(value, StringLiteral), value
    return json.loads('"' + value.text + '"')


def decoder_program(identifier, data, cut, policy):
    return f'''!(let $initial (str:utf8-new {policy})
      (case (str:utf8-feed $initial {raw(data[:cut])} False)
        (((Utf8DecodedV1 $first $state)
          (case (str:utf8-feed $state {raw(data[cut:])} True)
            (((Utf8DecodedV1 $last (Utf8DecoderV1 $p $tail $seen))
              (let $text (str:concat $first $last)
                (let $escaped (str:escape json $text)
                  (let $tail-size (str:byte-length $tail)
                    (function (return (DecoderVectorV1 {identifier} $escaped $seen $tail-size)))))))
             ((StrRefusedV1 $call (StrMalformedUtf8V1 $offset))
              (function (return (DecoderFaultV1 {identifier} $offset)))))))
         ((StrRefusedV1 $call (StrMalformedUtf8V1 $offset))
          (function (return (DecoderFaultV1 {identifier} $offset)))))))\n'''


HELPERS = '''
(: stream-test:rows (-> %Undefined% Atom %Undefined%))
(= (stream-test:rows $id $rows)
  (case $rows
    ((LNil True)
     ((LCons (LineV1 $number $text) $rest)
      (let $escaped (str:escape json $text)
        (let $shown (println! (LineVectorV1 $id $number $escaped))
          (stream-test:rows $id $rest)))))))
'''


def lines_program(identifier, text, cut):
    first, last = text[:cut], text[cut:]
    return f'''!(let $buffer (str:line-new)
      (let $first (str:line-segments {raw(first.encode('utf-8'))} False)
        (case (str:collect-lines $buffer $first False)
          (((LinesCollectedV1 $rows $next)
            (let $shown (stream-test:rows {identifier} $rows)
              (case $next
                (((LineBufferV1 $fragments $number $cr)
                  (let $last (str:line-segments {raw(last.encode('utf-8'))} $cr)
                    (case (str:collect-lines $next $last True)
                      (((LinesCollectedV1 $tail $finished)
                        (let $shown-tail (stream-test:rows {identifier} $tail)
                          (function (return (LinesFinishedV1 {identifier})))))))))))))))))\n'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/text-streaming-v1-tests")
    parser.add_argument("--language", action="append", choices=("he", "petta", "prime"))
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    count = 0

    def run(language, name, source=None, *, example=False, path=None):
        nonlocal count
        if example:
            script = ROOT / "tests/filesystem_v1" / f"{name}.metta"
        else:
            script = output / f"{language}-{name}.metta"
            script.write_text(source, encoding="utf-8")
        command = [str(binary), "--lang", language]
        if language != "prime":
            command += ["--profile", "extended"]
        result = subprocess.run(command + [str(script)] + ([str(path)] if path is not None else []), cwd=ROOT,
                                capture_output=True, timeout=90)
        (output / f"{language}-{name}.stdout").write_bytes(result.stdout)
        (output / f"{language}-{name}.stderr").write_bytes(result.stderr)
        count += 1
        assert result.returncode == 0, (language, name, result.returncode, result.stdout, result.stderr)
        assert b"forbidden" not in result.stdout, (language, name, result.stdout)
        return result.stdout

    prefix = "!(import! &self str)\n"
    rng = random.Random(20261014)
    buffers = [b"", b"\x00", bytes(range(256)), b"a\xc3\xa9\x00z", b"a\xe2\x80\xa8b",
               b"\xe2\x80", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"a\xffb",
               b"\xe0\x80a\xc2\x85", b"\xf0\x90\x80\x80", b"\xe2a\xc3\xa9"]
    buffers += [rng.randbytes(rng.randrange(1, 24)) for _ in range(24)]
    decoder_cases = []
    for data in buffers:
        for policy in ("strict", "ignore"):
            for cut in sorted({0, 1 if data else 0, len(data) // 2, len(data)}):
                reference = codecs.getincrementaldecoder("utf-8")(errors=policy)
                try:
                    first = reference.decode(data[:cut], final=False)
                    pending, _ = reference.getstate()
                    try:
                        last = reference.decode(data[cut:], final=True)
                        expected = ("data", first + last, len(data))
                    except UnicodeDecodeError as error:
                        expected = ("fault", cut - len(pending) + error.start)
                except UnicodeDecodeError as error:
                    expected = ("fault", error.start)
                decoder_cases.append((data, cut, policy, expected))
    texts = ["", "\n", "\r", "\r\n", "a\r\n\nb", "a\x00b\v\f\x1c\x1d\x1e",
             "one\x85\u2028three\u2029", "é\r\n字\n", "last", "a\n\n"]
    line_cases = [(text, cut, text.splitlines()) for text in texts for cut in range(len(text) + 1)]
    pattern = "sur" + "face"
    search_cases = [("İıſK", "i"), ("İıſK", "k"), ("ſur" + "face", pattern),
                    ("x" + pattern.upper() + "y", pattern), ("xababababac", "ababac"),
                    ("file.name", "."), ("abc", ""), ("abc", "abd"), ("SS", "s"), ("ß", "ss")]
    for _ in range(64):
        text = "".join(rng.choice("abABİıſKé\x00.") for _ in range(rng.randrange(0, 60)))
        needle = "".join(rng.choice("abiKs.") for _ in range(rng.randrange(0, 9)))
        search_cases.append((text, needle))

    for language in args.language or ("he", "petta", "prime"):
        for name, marker in (
            ("utf8_chunks_positive", b"Utf8Chunks"),
            ("utf8_empty_positive", b"Utf8Empty"),
            ("utf8_malformed_negative", b"StrMalformedUtf8V1 1"),
            ("lines_crlf_positive", b"LinesCRLF"),
            ("lines_unicode_positive", b"LineV1 3"),
            ("lines_malformed_negative", b"StrMalformedUtf8V1 5"),
            ("literal_unicode_positive", b"LiteralUnicode 0 6"),
            ("literal_overlap_positive", b"LiteralOverlap 5 4"),
            ("literal_pattern_negative", b"StrAsciiLiteralRequiredV1 0"),
            ("line_views_crlf_positive", b'LineV1 2 ""'),
            ("line_views_prefix_positive", b'LineV1 1 "held-tail"'),
            ("line_views_malformed_negative", b"StrMalformedUtf8V1 3"),
        ):
            observed = run(language, name, example=True)
            assert marker in observed, (language, name, observed)

        source = prefix + "".join(decoder_program(i, data, cut, policy)
                                   for i, (data, cut, policy, _) in enumerate(decoder_cases))
        records = values(run(language, "decoder-vectors", source))
        assert len(records) == len(decoder_cases), (language, "decoder count", len(records))
        for record, (_, _, _, expected) in zip(records, decoder_cases):
            count += 1
            if expected[0] == "data":
                assert headed(record, "DecoderVectorV1") and decoded(record[2]) == expected[1], (language, record, expected)
                assert record[3:] == (expected[2], 0), (language, record, expected)
            else:
                assert headed(record, "DecoderFaultV1") and record[2] == expected[1], (language, record, expected)

        source = prefix + HELPERS + "".join(lines_program(i, text, cut)
                                            for i, (text, cut, _) in enumerate(line_cases))
        records = values(run(language, "line-vectors", source))
        observed_lines = [[] for _ in line_cases]
        finished = set()
        for record in records:
            if headed(record, "LineVectorV1"):
                index, number = record[1:3]
                assert number == len(observed_lines[index]) + 1, record
                observed_lines[index].append(decoded(record[3]))
            else:
                assert headed(record, "LinesFinishedV1"), record
                assert record[1] not in finished, record
                finished.add(record[1])
        assert finished == set(range(len(line_cases))), (language, "line completion", finished)
        for found, (_, _, expected) in zip(observed_lines, line_cases):
            count += 1
            assert found == expected, (language, found, expected)

        source = prefix + HELPERS
        starts = []
        for index, (text, cut, _) in enumerate(line_cases):
            first, last = text[:cut], text[cut:]
            starts.append(len((first + "guard").splitlines()) - 1)
            source += f'''!(let $initial (str:line-new)
              (let $advanced (str:advance-lines $initial {raw(first.encode("utf-8"))} False)
                (case (str:feed-lines $advanced {raw(last.encode("utf-8"))} True)
                  (((LinesCollectedV1 $rows $finished)
                    (let $shown (stream-test:rows {index} $rows)
                      (function (return (LinesFinishedV1 {index})))))))))\n'''
        projected = [[] for _ in line_cases]
        finished = set()
        for record in values(run(language, "line-view-vectors", source)):
            if headed(record, "LineVectorV1"):
                index, number = record[1:3]
                assert number == starts[index] + len(projected[index]) + 1, record
                projected[index].append(decoded(record[3]))
            else:
                assert headed(record, "LinesFinishedV1"), record
                assert record[1] not in finished, record
                finished.add(record[1])
        assert finished == set(range(len(line_cases))), finished
        for index, (_, _, expected) in enumerate(line_cases):
            count += 1
            assert projected[index] == expected[starts[index]:], (language, index, projected[index], expected)

        source = prefix
        for index, (text, needle) in enumerate(search_cases):
            source += f'!(let $found (str:find-ascii-uncased {raw(text.encode("utf-8"))} {raw(needle.encode("ascii"))}) ' \
                      f'(function (return (LiteralVectorV1 {index} $found))))\n'
        records = values(run(language, "literal-vectors", source))
        assert len(records) == len(search_cases), (language, "literal count", records)
        for record, (text, needle) in zip(records, search_cases):
            count += 1
            match = re.search(re.escape(needle), text, re.IGNORECASE)
            if match:
                assert record[2] == len(text[:match.start()].encode("utf-8")), (language, record, text, needle)
            else:
                assert headed(record[2], "TextNotFound"), (language, record, text, needle)

        # Input names must never lose invalid bytes, while content decoding
        # explicitly selected as ignore may join the valid neighbours.
        source = prefix + f'!(let $raw {raw(b"a\xffb")} ' \
            + '(let $name (str:find-ascii-uncased $raw "ab") ' \
            + '(let $decoder (str:utf8-new ignore) ' \
            + '(case (str:utf8-feed $decoder $raw True) ' \
            + '(((Utf8DecodedV1 $text $state) ' \
            + '(let $content (str:find-ascii-uncased $text "ab") ' \
            + '(function (return (NameAndContent $name $content))))))))))\n'
        observed = run(language, "filename-content-policies", source)
        assert b'TextNotFound "ab"' in observed and b' 0)' in observed, observed

        for name, content, marker in (
            ("text_file_utf8_positive", b"a\xc3\xa9\n", b"FileUtf8 LNil (LCons (LineV1 1"),
            ("text_file_crlf_positive", b"\r\n\n", b'FileCRLF (LCons (LineV1 1 "")'),
            ("text_file_truncated_negative", b"a\xc3", b"StrMalformedUtf8V1 1"),
        ):
            path = output / (name + ".bin")
            path.write_bytes(content)
            observed = run(language, name, example=True, path=path)
            assert marker in observed, (language, name, observed)

        hostile = prefix + '''
(= (Utf8DecoderV1 $p $tail $seen) (println! forbidden-decoder))
(= (Utf8DecodedV1 $text $state) (println! forbidden-decoded))
(= (LineBufferV1 $fragments $number $cr) (println! forbidden-buffer))
(= (LineSegmentsV1 $rows $cr) (println! forbidden-segments))
(= (LinePartV1 $text $complete) (println! forbidden-part))
(= (LinesCollectedV1 $rows $buffer) (println! forbidden-collected))
(= (StrRefusedV1 $call $detail) (println! forbidden-refusal))
'''
        # PeTTa intentionally computes function-headed patterns, and HE
        # interprets some observer results in computation positions. Compare
        # native data publication and native-to-native handoff directly; a
        # literal-pattern claim would conflate those separate contracts.
        data_program = f'''!(let $initial (str:utf8-new strict)
          (let $value (str:utf8-feed $initial {raw(b"a\xc3\xa9\x00z")} True)
            (function (return (HostileDataV1 $value)))))
        !(let $initial (str:utf8-new strict)
          (let $fault (str:utf8-feed $initial {raw(b"a\xffb")} True)
            (function (return (HostileFaultV1 $fault)))))
        !(let $initial (str:line-new)
          (let $segments (str:line-segments "one\\n\\nlast" False)
            (let $collected (str:collect-lines $initial $segments True)
              (function (return (HostileLinesV1 $collected))))))
        !(let $initial (str:line-new)
          (let $advanced (str:advance-lines $initial "held" False)
            (let $collected (str:feed-lines $advanced "-tail\\n" True)
              (function (return (HostileViewsV1 $collected))))))\n'''
        ordinary = run(language, "native-data-control", prefix + data_program)
        defended = run(language, "hostile-decoders", hostile + data_program)
        assert ordinary == defended, (language, "host equations changed native data", ordinary, defended)
        assert b"HostileDataV1" in defended and b"HostileFaultV1" in defended and b"HostileLinesV1" in defended, defended
        # An observer may deliberately number a selected window from zero.
        # One-based numbering is the default, not a validity restriction.
        source = prefix + '!(let $buffer (function (return (LineBufferV1 LNil 0 False))) ' \
            + '(let $segments (str:line-segments "window\\n" False) ' \
            + '(str:collect-lines $buffer $segments True)))\n'
        observed = run(language, "zero-based-window", source)
        assert b'LineV1 0 "window"' in observed, observed
    print(f"(TextStreamingV1Summary {count} 0)")


if __name__ == "__main__":
    main()
