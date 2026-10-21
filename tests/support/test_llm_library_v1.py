#!/usr/bin/env python3
"""Offline protocol fixtures. No accounts, paid requests or application state."""
import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BIN = os.environ.get("CETTA_BIN", str(ROOT / "cetta"))
sys.path.insert(0, str(ROOT / "tools"))
from library_spelling import spelling


def quoted(value):
    return json.dumps(value, ensure_ascii=False)


class CodecFixtures(unittest.TestCase):
    def run_forms(self, lang, expressions, library=None):
        with tempfile.TemporaryDirectory(prefix="cetta-llm-v1-") as directory:
            path = Path(directory) / "fixture.metta"
            library = library or ROOT / "lib" / ("petta" if lang == "petta" else "") / "llm.metta"
            source = f'!(import! &self {quoted(str(library))})\n'
            # Convert only successful values for the fixture reader. Failure
            # codes are public symbols, never provider bodies or diagnostics.
            source += '''(: fixture:show (-> Atom %Undefined%))
(= (fixture:show $outcome)
   (case $outcome
     (((llm:v1:ok (llm:v1:encoded $path $body)) $body)
      ((llm:v1:ok $value) (llm:v1:render $value))
      ((llm:v1:error $code) (str:concat "ERROR:" (str:symbol->string $code)))
      ($other "INVALID-OUTCOME"))))
'''
            for expression in expressions:
                source += f'!(let $outcome {expression} (fixture:show $outcome))\n'
            path.write_text(source)
            result = subprocess.run([BIN, "--lang", lang, "--import-mode", "ancestor-walk", str(path)],
                                    cwd=ROOT, capture_output=True, text=True, timeout=45)
            self.assertEqual(result.returncode, 0, result.stderr[-1200:])
            output = []
            for line in result.stdout.splitlines():
                if not line.startswith(('"', '["')):
                    continue
                value = json.loads(line)
                if isinstance(value, list):
                    self.assertEqual(len(value), 1)
                    value = value[0]
                output.append(value if value.startswith("ERROR:") else json.loads(value))
            self.assertEqual(len(output), len(expressions), result.stdout[-1800:])
            return output

    def encode(self, protocol, request):
        text = request if isinstance(request, str) else json.dumps(request, ensure_ascii=False)
        return f'(llm:v1:encode {quoted(protocol)} {quoted(text)})'

    def decode(self, protocol, response):
        text = response if isinstance(response, str) else json.dumps(response, ensure_ascii=False)
        return f'(llm:v1:decode {quoted(protocol)} {quoted(text)})'

    def each_dialect(self, expressions):
        he = self.run_forms("he", expressions)
        petta = self.run_forms("petta", expressions)
        self.assertEqual(he, petta)
        return he

    def request(self):
        return {"model": "fixture-model", "max_tokens": 42, "messages": [
            {"role": "system", "content": "system"},
            {"role": "developer", "content": "developer"},
            {"role": "user", "content": "input"}]}

    def test_capabilities_distinguish_codec_and_model_support(self):
        values = self.each_dialect([f'(llm:v1:capabilities {quoted(protocol)})'
            for protocol in ("chat-completions", "responses", "anthropic", "unsupported")])
        for value in values[:3]:
            self.assertTrue(value["buffered"])
            self.assertTrue(value["native_replay"])
            self.assertFalse(value["streaming"])
            self.assertFalse(value["tool_execution"])
            self.assertIsNone(value["model_capabilities"])
            self.assertEqual(value["tool_schema_validation"], "object-shape-only")
        self.assertEqual(values[-1], "ERROR:unsupported-protocol")

    def anthropic(self):
        return {"id": "response-a", "role": "assistant", "stop_reason": "tool_use", "content": [
            {"type": "thinking", "thinking": "opaque", "signature": "signed\u0000signature"},
            {"type": "text", "text": "before"},
            {"type": "tool_use", "id": "call-a", "name": "nop", "input": {"x": 1}},
            {"type": "text", "text": "after"},
            {"type": "redacted_thinking", "data": "opaque-bytes"}],
            "usage": {"input_tokens": 9, "output_tokens": 3, "cache_read_input_tokens": 7}}

    def test_order_identity_native_signatures_and_unknown_usage(self):
        wire = self.anthropic()
        result = self.each_dialect([self.decode("anthropic", wire)])[0]
        parts = result["items"][0]["parts"]
        self.assertEqual([p["kind"] for p in parts], ["reasoning", "text", "tool-call", "text", "reasoning"])
        self.assertEqual([p["native"] for p in parts], wire["content"])
        self.assertEqual(parts[2]["call_id"], "call-a")
        self.assertEqual(json.loads(parts[2]["arguments"]), {"x": 1})
        self.assertEqual(parts[2]["execution_owner"], "caller")
        self.assertEqual(result["usage"], {"input": 9, "output": 3, "cache_read": 7,
            "cache_write": None, "reasoning": None, "native": wire["usage"]})
        self.assertEqual(result["native"], wire)
        self.assertEqual(result["termination"]["raw"], "tool_use")

    def test_roles_are_preserved_and_losses_require_policy(self):
        request = self.request()
        mapped = {**request, "role_policy": "merge-system-developer"}
        chat, responses, refusal, anthropic = self.each_dialect([
            self.encode("chat-completions", request), self.encode("responses", request),
            self.encode("anthropic", request), self.encode("anthropic", mapped)])
        self.assertEqual(chat["messages"], request["messages"])
        self.assertEqual(chat["max_completion_tokens"], request["max_tokens"])
        self.assertNotIn("max_tokens", chat)
        self.assertEqual(responses["input"], request["messages"])
        self.assertNotIn("instructions", responses)
        self.assertEqual(refusal, "ERROR:unsupported-developer-role")
        self.assertEqual([b["text"] for b in anthropic["system"]], ["system", "developer"])
        self.assertEqual(anthropic["system"][-1]["cache_control"], {"type": "ephemeral"})

    def test_conversation_caching_marks_the_newest_and_previous_user_turns(self):
        call = {"id": "call-a", "type": "function", "function": {"name": "nop", "arguments": "{}"}}
        request = {"model": "fixture-model", "max_tokens": 42, "messages": [
            {"role": "system", "content": "system"},
            {"role": "user", "content": "task"},
            {"role": "assistant", "content": "first", "tool_calls": [call]},
            {"role": "tool", "tool_call_id": "call-a", "content": "one"},
            {"role": "assistant", "content": "second", "tool_calls": [{**call, "id": "call-b"}]},
            {"role": "tool", "tool_call_id": "call-b", "content": "two"}]}
        opening = {**request, "messages": request["messages"][:2], "cache_messages": True}
        plain, cached, first, hour, wrong_ttl, elsewhere = self.each_dialect([
            self.encode("anthropic", request),
            self.encode("anthropic", {**request, "cache_messages": True}),
            self.encode("anthropic", opening),
            self.encode("anthropic", {**request, "cache_messages": True, "cache_ttl": "1h"}),
            self.encode("anthropic", {**request, "cache_ttl": "2h"}),
            self.encode("chat-completions", {**request, "cache_messages": True})])

        def marked(body):
            return [(index, position) for index, message in enumerate(body["messages"])
                    for position, block in enumerate(message["content"]) if "cache_control" in block]

        def unmarked(body):
            return [{**m, "content": [{k: v for k, v in b.items() if k != "cache_control"} for b in m["content"]]}
                    for m in body["messages"]]

        self.assertEqual(marked(plain), [])
        self.assertEqual(plain["system"][-1]["cache_control"], {"type": "ephemeral"})
        self.assertEqual(marked(cached), [(2, 0), (4, 0)])
        self.assertEqual(unmarked(cached), plain["messages"])
        self.assertEqual(marked(first), [(0, 0)])
        self.assertEqual(hour["system"][-1]["cache_control"], {"type": "ephemeral", "ttl": "1h"})
        self.assertEqual(hour["messages"][4]["content"][-1]["cache_control"], {"type": "ephemeral", "ttl": "1h"})
        self.assertEqual((wrong_ttl, elsewhere), ("ERROR:invalid-cache-policy", "ERROR:unsupported-provider-option"))

    def test_chat_reasoning_uses_an_explicit_wire_profile(self):
        request = {**self.request(), "reasoning": {"effort": "low"}}
        native, compatible, unknown = self.each_dialect([
            self.encode("chat-completions", request),
            self.encode("chat-completions", {**request, "chat_profile": "compatible"}),
            self.encode("chat-completions", {**request, "reasoning": {"context": "all_turns"}})])
        self.assertEqual(native["reasoning_effort"], "low")
        self.assertEqual(native["max_completion_tokens"], 42)
        self.assertNotIn("reasoning", native)
        self.assertEqual(compatible["reasoning"], {"effort": "low"})
        self.assertEqual(compatible["max_tokens"], 42)
        self.assertNotIn("reasoning_effort", compatible)
        self.assertEqual(unknown, "ERROR:unsupported-request-field")
        rejected = self.each_dialect([self.encode("responses", {**request, "chat_profile": "compatible"}),
            self.encode("chat-completions", {**request, "tool_choice": "required"})])
        self.assertEqual(rejected, ["ERROR:unsupported-chat-profile", "ERROR:tool-choice-without-tools"])

    def test_complete_responses_keep_items_and_unknowns(self):
        wire = {"id": "r", "status": "completed", "output": [
            {"type": "reasoning", "id": "reason", "summary": [], "encrypted_content": "cipher"},
            {"type": "message", "role": "assistant", "content": [
                {"type": "output_text", "text": "one"}, {"type": "refusal", "refusal": "no"}]},
            {"type": "function_call", "id": "item", "call_id": "call", "name": "nop", "arguments": "{}"},
            {"type": "future_item", "payload": {"preserve": True}}]}
        result = self.each_dialect([self.decode("responses", wire)])[0]
        self.assertEqual([i["kind"] for i in result["items"]], ["reasoning", "message", "tool-call", "opaque"])
        self.assertEqual([i["native"] for i in result["items"]], wire["output"])
        self.assertEqual(result["items"][2]["call_id"], "call")
        self.assertTrue(all(value is None for key, value in result["usage"].items()))

    def test_multiple_choices_and_unknown_stop_remain_visible(self):
        wire = {"choices": [{"index": 0, "message": {"role": "assistant", "content": "zero"}, "finish_reason": "stop"},
                            {"index": 1, "message": {"role": "assistant", "content": "one"}, "finish_reason": "future"}]}
        result = self.each_dialect([self.decode("chat-completions", wire)])[0]
        self.assertEqual([i["choice_index"] for i in result["items"]], [0, 1])
        self.assertEqual(result["items"][1]["termination"], {"normalized": "unknown", "raw": "future", "details": None})

    def test_empty_provider_containers_accept_whitespace(self):
        wire = {"id": "whitespace", "choices": [{"index": 0, "message": {
            "role": "assistant", "content": "ok", "tool_calls": []},
            "finish_reason": "stop"}], "extension": {}}
        text = json.dumps(wire).replace("[]", "[ ]").replace("{}", "{ }")
        result = self.each_dialect([self.decode("chat-completions", text)])[0]
        self.assertEqual(result["native"], wire)
        self.assertEqual(result["items"][0]["parts"][0]["text"], "ok")

    def test_large_provider_text_is_preserved_exactly(self):
        text = "response" * 8192
        wire = {"id": "large", "choices": [{"index": 0, "message": {
            "role": "assistant", "content": text}, "finish_reason": "stop"}]}
        result = self.each_dialect([self.decode("chat-completions", wire)])[0]
        self.assertEqual(result["native"], wire)
        self.assertEqual(result["items"][0]["parts"][0]["text"], text)

    def test_malformed_known_blocks_and_duplicates_are_errors(self):
        expressions = [self.decode("anthropic", {"content": [{"type": "text", "text": 3}]}),
            self.decode("anthropic", {"content": [{"type": "thinking", "thinking": "text"}]}),
            self.decode("anthropic", '{"content":[],"content":[{"type":"text","text":"private-fixture"}]}'),
            self.encode("chat-completions", {**self.request(), "temperature": .5}),
            self.encode("chat-completions", {**self.request(), "stream": True}),
            self.encode("chat-completions", {**self.request(), "extensions": {"model": "other"}})]
        for result in self.each_dialect(expressions):
            self.assertIsInstance(result, str)
            self.assertTrue(result.startswith("ERROR:"), result)
            self.assertNotIn("private-fixture", result)

    def test_nested_and_escaped_duplicate_keys_remain_rejected(self):
        wire = ('{"content":[{"type":"tool_use","id":"call","name":"nop",'
                '"input":{"key":1,"\\u006bey":2}}],"stop_reason":"tool_use"}')
        self.assertEqual(self.each_dialect([self.decode("anthropic", wire)]),
                         ["ERROR:invalid-or-ambiguous-json"])

    def test_render_failure_is_a_total_short_outcome(self):
        self.assertEqual(self.each_dialect(['(llm:v1:render-outcome (JsonNumber "01"))']),
                         ["ERROR:result-render-error"])

    def test_known_field_types_roles_and_cross_protocol_attachments(self):
        malformed = [self.decode("chat-completions", {"choices": [], "usage": "invented"}),
            self.decode("anthropic", {"content": [], "id": 3}),
            self.encode("responses", {**self.request(), "role_policy": "invented"}),
            self.encode("chat-completions", {**self.request(), "thinking": {"type": "adaptive"}}),
            self.encode("chat-completions", {**self.request(), "model": ""}),
            self.encode("chat-completions", {**self.request(), "messages": [{"role": "invented", "content": "x"}]}),
            self.encode("responses", {**self.request(), "messages": [{"role": "assistant", "native_content": {
                "protocol": "anthropic", "protocol_version": "2023-06-01", "value": []}}]})]
        self.assertTrue(all(isinstance(value, str) and value.startswith("ERROR:")
                            for value in self.each_dialect(malformed)))

    def test_unfinished_item_cannot_project_as_a_completed_tool_batch(self):
        wire = {"status": "completed", "output": [{"type": "function_call", "status": "in_progress",
            "call_id": "c", "name": "nop", "arguments": '{"partial":'}]}
        decoded = self.each_dialect([self.decode("responses", wire)])[0]
        replay = self.each_dialect([f'(llm:v1:replay {quoted(json.dumps(decoded))} 0)'])[0]
        self.assertEqual(replay, "ERROR:unfinished-replay")
        expression = f'(llm:v1:project-chat {quoted(json.dumps(decoded))} 0)'
        for lang in ("he", "petta"):
            library = ROOT / "lib" / ("petta" if lang == "petta" else "") / "llm/compat.metta"
            self.assertEqual(self.run_forms(lang, [expression], library), ["ERROR:unfinished-tool-call"])

    def test_tool_choice_reasoning_limits_and_extension(self):
        request = {**self.request(), "role_policy": "merge-system-developer", "tools": [
            {"type": "function", "function": {"name": "nop", "parameters": {"type": "object"}}}],
            "tool_choice": "required", "reasoning": {"effort": "low"}}
        forced, refused, explicit = self.each_dialect([self.encode("anthropic", request),
            self.encode("anthropic", {**request, "thinking": {"type": "adaptive"}}),
            self.encode("anthropic", {**request, "thinking": {"type": "adaptive"},
                                    "tool_choice_policy": "auto-for-thinking"})])
        self.assertEqual(forced["tool_choice"], {"type": "any"})
        self.assertEqual(refused, "ERROR:unsupported-thinking-tool-choice")
        self.assertEqual(explicit["tool_choice"], {"type": "auto"})
        self.assertEqual(explicit["output_config"], {"effort": "low"})
        invalid = self.each_dialect([self.encode("responses", {**self.request(), "max_tokens": limit})
                                    for limit in (0, -1, 1.5, "42")])
        self.assertTrue(all(isinstance(r, str) and r.startswith("ERROR:") for r in invalid))

    def test_native_replay_and_cross_provider_rejection(self):
        decoded = self.each_dialect([self.decode("anthropic", self.anthropic())])[0]
        replay = self.each_dialect([f'(llm:v1:replay {quoted(json.dumps(decoded))} 0)'])[0]
        request = {"model": "fixture", "max_tokens": 12, "messages": [replay]}
        actual = self.each_dialect([self.encode("anthropic", request)])[0]
        self.assertEqual(actual["messages"][0]["content"], self.anthropic()["content"])
        wrong = copy.deepcopy(replay)
        wrong["native_content"]["protocol"] = "responses"
        result = self.each_dialect([self.encode("anthropic", {**request, "messages": [wrong]})])[0]
        self.assertEqual(result, "ERROR:incompatible-native-attachment")

    def test_truncation_keeps_incomplete_arguments_and_refuses_replay(self):
        wire = {"status": "incomplete", "incomplete_details": {"reason": "max_output_tokens"}, "output": [
            {"type": "function_call", "call_id": "c", "name": "nop", "arguments": '{"partial":'}]}
        decoded = self.each_dialect([self.decode("responses", wire)])[0]
        self.assertEqual(decoded["items"][0]["status"], "incomplete")
        self.assertEqual(decoded["termination"]["normalized"], "length")
        replay = self.each_dialect([f'(llm:v1:replay {quoted(json.dumps(decoded))} 0)'])[0]
        self.assertEqual(replay, "ERROR:unfinished-replay")

    def test_symbol_text_unicode_nul_and_command_text_are_data(self):
        wire = {"content": [{"type": "text", "text": '(println! SHOULD-NOT-RUN) Žluťoučký\u0000馬'}], "stop_reason": "end_turn"}
        literal = quoted(json.dumps(wire, ensure_ascii=False))
        expressions = [self.decode("anthropic", wire),
            f'(let $symbol (str:string->symbol {literal}) (llm:v1:decode anthropic $symbol))']
        first, second = self.each_dialect(expressions)
        self.assertEqual(first, second)
        self.assertEqual(first["items"][0]["parts"][0]["text"], wire["content"][0]["text"])

    def test_three_semantic_mutants_are_detected(self):
        source = (ROOT / "lib/llm.metta").read_text()
        mutants = [("order", "(cons-atom $part $rest)", "(cons-atom $part ())"),
                   ("identity", '(JsonPair "call_id" $id)', '(JsonPair "call_id" (JsonString "wrong"))'),
                   ("usage", r'\(if\s+\(== \$value JsonNull\)\s+JsonNull',
                    '(if (== $value JsonNull) (JsonNumber "0")')]
        expression = self.decode("anthropic", self.anthropic())
        baseline = self.run_forms("petta", [expression])[0]
        with tempfile.TemporaryDirectory(prefix="llm-mutants-") as directory:
            for name, old, new in mutants:
                if name == "usage":
                    mutated, count = re.subn(old, lambda _: new, source)
                    self.assertGreater(count, 0, "usage mutation site disappeared")
                else:
                    self.assertIn(old, source, f"{name} mutation site disappeared")
                    mutated = source.replace(old, new)
                path = Path(directory) / f"{name}.metta"
                path.write_text(spelling(mutated, "petta"))
                mutant = self.run_forms("petta", [expression], path)[0]
                self.assertNotEqual(mutant, baseline, name)
                if name == "order":
                    self.assertNotEqual(len(mutant["items"][0]["parts"]), 5)
                elif name == "identity":
                    self.assertNotEqual(mutant["items"][0]["parts"][2]["call_id"], "call-a")
                else:
                    self.assertIsNotNone(mutant["usage"]["cache_write"])

    def test_command_profile_and_explicit_cache_identity(self):
        request = {"model": "fixture", "max_tokens": 42, "stable": ["one", "two"],
                   "user": "input", "reasoning": {"effort": "low"}, "cache_key": "caller-cache-identity"}
        expressions = [f'(llm:v1:encode-command {quoted(p)} {quoted(json.dumps(request))})'
                       for p in ("anthropic", "responses", "chat-completions")]
        expected = [
            {"model": "fixture", "max_tokens": 42, "messages": [{"role": "user", "content": "input"}],
             "system": [{"type": "text", "text": text, "cache_control": {"type": "ephemeral"}}
                        for text in ("one", "two")], "output_config": {"effort": "low"}},
            {"model": "fixture", "max_output_tokens": 42, "input": "input", "store": False,
             "instructions": "onetwo", "prompt_cache_key": "caller-cache-identity", "reasoning": {"effort": "low"}},
            {"model": "fixture", "max_tokens": 42, "messages": [{"role": "system", "content": "onetwo"},
                {"role": "user", "content": "input"}], "reasoning": {"effort": "low"}}]
        for lang in ("he", "petta"):
            library = ROOT / "lib" / ("petta" if lang == "petta" else "") / "llm/command.metta"
            self.assertEqual(self.run_forms(lang, expressions, library), expected)

    def test_explicit_legacy_projection_retains_calls_but_rejects_unknown_items(self):
        responses = {"status": "completed", "output": [
            {"type": "reasoning", "id": "r", "summary": [], "encrypted_content": "opaque"},
            {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": "before"}]},
            {"type": "function_call", "call_id": "c", "name": "nop", "arguments": "{}"}]}
        unknown = {"status": "completed", "output": [{"type": "future_item", "opaque": True}]}
        refusal = {"status": "completed", "output": [{"type": "message", "role": "assistant",
                    "content": [{"type": "refusal", "refusal": "no"}]}]}
        decoded = self.each_dialect([self.decode("anthropic", self.anthropic()),
                                    self.decode("responses", responses), self.decode("responses", unknown),
                                    self.decode("responses", refusal)])
        expressions = [f'(llm:v1:project-chat {quoted(json.dumps(value))} 0)' for value in decoded]
        for protocol, wire in (("anthropic", self.anthropic()), ("responses", responses),
                               ("responses", unknown), ("responses", refusal)):
            expressions.append(f'(let $decoded-fixture {self.decode(protocol, wire)} '
                '(case $decoded-fixture (((llm:v1:ok $canonical-fixture) '
                '(llm:v1:project-chat-value $canonical-fixture 0)) ($unexpected-fixture $unexpected-fixture))))')
        outputs = []
        for lang in ("he", "petta"):
            library = ROOT / "lib" / ("petta" if lang == "petta" else "") / "llm/compat.metta"
            outputs.append(self.run_forms(lang, expressions, library))
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(outputs[0][:4], outputs[0][4:])
        anthropic, response, refused, rejection = outputs[0][:4]
        self.assertEqual(anthropic["choices"][0]["message"]["content"], "beforeafter")
        self.assertEqual(anthropic["choices"][0]["message"]["tool_calls"][0]["id"], "call-a")
        self.assertEqual(anthropic["choices"][0]["message"]["reasoning_details"],
                         [self.anthropic()["content"][0], self.anthropic()["content"][4]])
        self.assertEqual(response["choices"][0]["finish_reason"], "tool_calls")
        self.assertEqual(response["usage"], {})
        self.assertEqual(refused, "ERROR:unsupported-legacy-item")
        self.assertEqual(rejection["choices"][0]["message"]["refusal"], "no")
        self.assertEqual(rejection["choices"][0]["finish_reason"], "refusal")


if __name__ == "__main__":
    unittest.main()
