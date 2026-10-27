#!/usr/bin/env python3
"""lib/crypto against the official BIP-340 and NIP-44 v2 vectors, in PeTTa, HE and Prime.

The vectors are the BIP-340 reference set (github.com/bitcoin/bips,
bip-0340/test-vectors.csv) and the NIP-44 v2 set (github.com/paulmillr/nip44,
nip44.vectors.json), in tests/support/fixtures/nostr.

- BIP-340: the public key of every secret key, the signature made with the
  given auxiliary randomness, and the verification result of every row,
  invalid keys and signatures included.
- NIP-44: every conversation key, computed from both sides; every
  encryption, byte for byte, and its decryption; the long messages by digest;
  the plaintext lengths that must be refused; every invalid conversation key
  and every payload that must not open, each with its reason.
- Random bytes: the requested length, different between calls.
"""
import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/support/fixtures/nostr"
IMPORTS = {
    "petta": "!(import! &self (library crypto))\n!(import! &self (library str))\n",
    "he": "!(import! &self crypto)\n!(import! &self str)\n",
    "prime": "!(import! &self crypto)\n!(import! &self str)\n",
}
NOT_UTF8_PAYLOAD = ("As3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3Nzc3NGufyFiRwKjwgVURiP2cm6ZWJ7pOrWUvF/hPmzAQyzq/"
                    "BpH8Qe/0EZKJ/QKZ17b1ADmXtXf9P73sXFVb3NkMYEJOZ")
DECRYPT_REASONS = {
    "unknown encryption version": "crypto:unknown-version",
    "unknown encryption version 0": "crypto:unknown-version",
    "invalid base64": "crypto:invalid-payload",
    "invalid MAC": "crypto:invalid-mac",
    "invalid padding": "crypto:invalid-padding",
    # NIP-44 reads an empty payload as an unknown version before its size.
    "invalid payload length: 0": "crypto:unknown-version",
}


def literal(text):
    escapes = {'"': '\\"', "\\": "\\\\", "\n": "\\n", "\r": "\\r", "\t": "\\t"}
    if any(ord(c) < 0x20 and c not in escapes for c in text):
        raise ValueError("a control character the readers do not escape alike")
    return '"' + "".join(escapes.get(c, c) for c in text) + '"'


def failed(kind):
    return "failed " + kind


def normalize(text):
    """Strings unquoted, booleans in lower case, refusals by their kind."""
    match = re.match(r'^\(crypto:failed (\S+) ', text)
    if match:
        return failed(match.group(1))
    if text in ("True", "true", "False", "false"):
        return text.lower()
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        try:
            return json.loads(text)
        except ValueError:
            return text
    return text


def run(binary, dialect, cases, work):
    program = work / ("crypto-%s.metta" % dialect)
    program.write_text(IMPORTS[dialect] + "".join("!(println! (RESULT %s))\n" % expression
                                                  for expression, _, _ in cases), encoding="utf-8")
    result = subprocess.run([str(binary.resolve()), "--lang", dialect, str(program)], capture_output=True,
                            encoding="utf-8", errors="replace", timeout=1200)
    if result.returncode:
        raise SystemExit("%s: engine exited %d: %s" % (dialect, result.returncode, result.stderr[-2000:]))
    results = [line[len("(RESULT "):-1] for line in result.stdout.split("\n") if line.startswith("(RESULT ")]
    if len(results) != len(cases):
        raise SystemExit("%s: expected %d results, read %d" % (dialect, len(cases), len(results)))
    return [(name, normalize(text), want) for (_, name, want), text in zip(cases, results)]


def bip340_cases():
    cases = []
    with open(FIXTURES / "bip340-vectors.csv", newline="") as rows:
        for row in csv.DictReader(rows):
            index, secret, public = row["index"], row["secret key"], row["public key"].lower()
            message, signature = row["message"].lower(), row["signature"].lower()
            if secret:
                cases.append(("(crypto:xonly-public-key %s)" % literal(secret),
                              "bip340 %s public key" % index, public))
                cases.append(("(crypto:schnorr-sign %s %s %s)" % (literal(secret), literal(message),
                                                                 literal(row["aux_rand"])),
                              "bip340 %s signature" % index, signature))
            cases.append(("(crypto:schnorr-verify %s %s %s)" % (literal(public), literal(message),
                                                               literal(signature)),
                          "bip340 %s verification" % index, row["verification result"].lower()))
    return cases


def nip44_cases():
    vectors = json.loads((FIXTURES / "nip44.vectors.json").read_text())["v2"]
    valid, invalid = vectors["valid"], vectors["invalid"]
    cases = []
    for i, case in enumerate(valid["get_conversation_key"]):
        cases.append(("(crypto:nip44-conversation-key %s %s)" % (literal(case["sec1"]), literal(case["pub2"])),
                      "conversation key %d" % i, case["conversation_key"]))
    for i, case in enumerate(valid["encrypt_decrypt"]):
        key, nonce = literal(case["conversation_key"]), literal(case["nonce"])
        for first, second in (("sec1", "sec2"), ("sec2", "sec1")):
            cases.append(("(crypto:nip44-conversation-key %s (crypto:xonly-public-key %s))"
                          % (literal(case[first]), literal(case[second])),
                          "conversation key of %s, message %d" % (first, i), case["conversation_key"]))
        cases.append(("(crypto:nip44-encrypt %s %s %s)" % (key, literal(case["plaintext"]), nonce),
                      "encryption %d" % i, case["payload"]))
        cases.append(("(str:sha256 (crypto:nip44-decrypt %s %s))" % (key, literal(case["payload"])),
                      "decryption %d" % i, hashlib.sha256(case["plaintext"].encode()).hexdigest()))
    for i, case in enumerate(valid["encrypt_decrypt_long_msg"]):
        key, nonce = literal(case["conversation_key"]), literal(case["nonce"])
        encrypted = "(crypto:nip44-encrypt %s %s %s)" % (key, literal(case["pattern"] * case["repeat"]), nonce)
        cases.append(("(str:sha256 %s)" % encrypted, "long encryption %d" % i, case["payload_sha256"]))
        cases.append(("(str:sha256 (crypto:nip44-decrypt %s %s))" % (key, encrypted),
                      "long decryption %d" % i, case["plaintext_sha256"]))
    key, nonce = literal("ab" * 32), literal("cd" * 32)
    for length in invalid["encrypt_msg_lengths"]:
        cases.append(("(crypto:nip44-encrypt %s %s %s)" % (key, literal("a" * length), nonce),
                      "plaintext of %d bytes" % length, failed("crypto:invalid-plaintext-length")))
    for case in invalid["get_conversation_key"]:
        reason = "crypto:invalid-secret-key" if case["note"].startswith("sec1") else "crypto:invalid-public-key"
        cases.append(("(crypto:nip44-conversation-key %s %s)" % (literal(case["sec1"]), literal(case["pub2"])),
                      "invalid conversation key: %s" % case["note"], failed(reason)))
    for case in invalid["decrypt"]:
        reason = DECRYPT_REASONS.get(case["note"], "crypto:invalid-payload")
        cases.append(("(crypto:nip44-decrypt %s %s)" % (literal(case["conversation_key"]), literal(case["payload"])),
                      "payload that must not open: %s" % case["note"], failed(reason)))
    return cases


def other_cases():
    return [
        ("(crypto:random-hex 0)", "no random bytes", ""),
        ("(crypto:random-hex -1)", "a negative byte count", failed("crypto:invalid-argument")),
        ("(crypto:random-hex 65537)", "too many random bytes", failed("crypto:invalid-argument")),
        ('(crypto:schnorr-verify "zz" "00" "00")', "text that is not hex verifies nothing", "false"),
        ("(crypto:xonly-public-key 3)", "a number is not a key", failed("crypto:invalid-hex")),
        ('(crypto:xonly-public-key "%s")' % ("00" * 32), "the zero key", failed("crypto:invalid-secret-key")),
        ('(crypto:nip44-encrypt "%s" (str:byte-slice "\u00e9" 0 1) "%s")' % ("ab" * 32, "cd" * 32),
         "a plaintext that is not UTF-8", failed("crypto:invalid-utf8")),
        # The payload of the single byte C3 under key ab...ab and nonce cd...cd.
        ('(crypto:nip44-decrypt "%s" "%s")' % ("ab" * 32, NOT_UTF8_PAYLOAD),
         "a payload whose plaintext is not UTF-8", failed("crypto:invalid-utf8")),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    parser.add_argument("--dialect", action="append", choices=sorted(IMPORTS))
    arguments = parser.parse_args()
    cases = bip340_cases() + nip44_cases() + other_cases()
    failures, total = [], 0
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        for dialect in arguments.dialect or ["petta", "he", "prime"]:
            for name, got, want in run(arguments.binary, dialect, cases, work):
                total += 1
                if got != want:
                    failures.append((dialect, name, got, want))
            randoms = run(arguments.binary, dialect, [("(crypto:random-hex 32)", "random", None)] * 2, work)
            total += 1
            if not (all(re.fullmatch(r"[0-9a-f]{64}", got) for _, got, _ in randoms)
                    and randoms[0][1] != randoms[1][1]):
                failures.append((dialect, "random bytes", [got for _, got, _ in randoms], "two fresh 32-byte values"))
    for dialect, name, got, want in failures[:40]:
        print("FAIL %s %s:\n  got  %r\n  want %r" % (dialect, name, str(got)[:200], str(want)[:200]))
    print("crypto vectors: %d checks, %d failures" % (total, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
