; JSON values from the RFC-8259 grammar. Ordinary ingestion and emission do
; not build parse forests or certify each input. Reference operations below
; explicitly select the authored ParserPack for diagnostics and qualification.
;
; Ordinary operations use JsonObject/JsonPair, JsonArray and JsonNull around
; native String, Int/BigInt, Float and Bool leaves. Object members retain
; order and duplicates. Exact scalar spellings belong to inspection.
; json:inspect explicitly expands strings into Unicode scalar atoms and
; records member occurrences/source spans. json:from-inspection forgets that
; metadata without a wire round trip. Reference calls additionally request
; the authored diagnostic parser. Failures are JsonFailureV1 data in every
; view, not dialect-specific engine exceptions.
;
; Every operation computes its arguments, like any function, and hands the
; computed values to its native as they are, through eval.  Each native
; declares its interface as the standard library declares its own
; value-taking builtins (repr, get-metatype): a JSON value is taken and
; returned as an Atom, text as a String.  The operation's result is a
; value, declared Atom so that it is not interpreted again.  A JSON value
; is therefore never evaluated inside the library, even where a program
; gives the JSON constructors equations.
;
; The arguments of an operation are computed by the language, before the
; library sees them.  In HE, as in upstream HE, an argument at a parameter
; that is not Atom-typed is interpreted at the call: a call is computed, and
; a value held in a variable is interpreted again, so equations a program
; gives the JSON constructors apply to it there.  PeTTa passes a variable's
; value on as it is.  Composed calls, (json:stringify (json:parse text)),
; hand the parsed value over untouched in both.
;
; This file is the HE spelling.  The PeTTa and Prime spellings under
; lib/petta and lib/prime are generated from it.

(: __cetta_lib_json_capabilities_v1 (-> Atom Atom))
(: __cetta_lib_json_parse_v1 (-> String Atom))
(: __cetta_lib_json_parse_v1 (-> String Atom Atom))
(: __cetta_lib_json_to_legacy_v1 (-> Atom Atom))
(: __cetta_lib_json_from_inspection_v1 (-> Atom Atom))
(: __cetta_lib_json_stringify_v1 (-> Atom Atom))
(: __cetta_lib_json_stringify_v1 (-> Atom Atom Atom))
(: __cetta_lib_json_members_v1 (-> Atom Atom))
(: __cetta_lib_json_members_v1 (-> Atom Atom Atom))
(: __cetta_lib_json_get_all_v1 (-> Atom String Atom))
(: __cetta_lib_json_get_all_v1 (-> Atom String Atom Atom))
(: __cetta_lib_json_get_first_v1 (-> Atom String Atom))
(: __cetta_lib_json_get_first_v1 (-> Atom String Atom Atom))
(: __cetta_lib_json_get_last_v1 (-> Atom String Atom))
(: __cetta_lib_json_get_last_v1 (-> Atom String Atom Atom))
(: __cetta_lib_json_parse (-> String Atom))
(: __cetta_lib_json_parse (-> String Atom Atom))
(: __cetta_lib_json_project_v1 (-> String Expression Atom))
(: __cetta_lib_json_stringify (-> Atom Atom))
(: __cetta_lib_json_stringify (-> Atom Atom Atom))
(: __cetta_lib_json_object_get (-> Atom String Atom))

(: json:capabilities (-> Atom))
(= (json:capabilities)
   (function
     (chain (eval (__cetta_lib_json_capabilities_v1 ())) $result
       (return $result))))

(: json:parse (-> %Undefined% Atom))
(= (json:parse $text)
   (function
     (chain (eval (__cetta_lib_json_parse $text)) $result
       (return $result))))

; json.loads as CPython 3.12 runs it, beside the strict json:parse. NaN,
; Infinity and -Infinity are floats; a repeated key keeps its first position
; and takes the last value; a lone surrogate escape stays in its string,
; encoded as Python's surrogatepass handler encodes it (str:python-codepoints
; reads it back). A Python exception is (JsonPythonErrorV1 class message ...)
; with message its str(): JSONDecodeError adds msg, pos, lineno and colno,
; positions counting code points; ValueError comes from int() past 4300 digits.
(: json:parse-python (-> %Undefined% Atom))
(= (json:parse-python $text)
   (function
     (chain (eval (__cetta_lib_json_parse $text JsonPythonV1)) $result
       (return $result))))

(: json:parse-reference (-> %Undefined% Atom))
(= (json:parse-reference $text)
   (function
     (chain (eval (__cetta_lib_json_parse $text JsonReferenceV1)) $result
       (return $result))))

; Parse the complete document and materialize only the value at a path of
; text keys and zero-based integer indices. Object steps choose the first
; occurrence, like json:get. Missing is JsonMissingV1; malformed input is a
; JsonFailureV1, so an explicit null remains distinguishable from both.
(: json:parse-at (-> %Undefined% %Undefined% Atom))
(= (json:parse-at $text $path)
   (function
     (chain (eval (__cetta_lib_json_project_v1 $text $path)) $result
       (return $result))))

(: json:inspect (-> %Undefined% Atom))
(= (json:inspect $text)
   (function
     (chain (eval (__cetta_lib_json_parse_v1 $text)) $result
       (return $result))))

(: json:inspect-reference (-> %Undefined% Atom))
(= (json:inspect-reference $text)
   (function
     (chain (eval (__cetta_lib_json_parse_v1 $text JsonReferenceV1)) $result
       (return $result))))

(: json:stringify-reference (-> %Undefined% Atom))
(= (json:stringify-reference $value)
   (function
     (chain (eval (__cetta_lib_json_stringify $value JsonReferenceV1)) $result
       (return $result))))

(: json:stringify (-> %Undefined% Atom))
(= (json:stringify $value)
   (function
     (chain (eval (__cetta_lib_json_stringify $value)) $result
       (return $result))))

(: json:pretty (-> %Undefined% Atom))
(= (json:pretty $value)
   (function
     (chain (eval (__cetta_lib_json_stringify $value JsonPrettyV1)) $result
       (return $result))))

; Python's json.dumps with default arguments, byte for byte: ", " and ": "
; separators, floats in Python's shortest repr, and ASCII escapes.
(: json:stringify-python (-> %Undefined% Atom))
(= (json:stringify-python $value)
   (function
     (chain (eval (__cetta_lib_json_stringify $value JsonPythonV1)) $result
       (return $result))))

; The same with json.dumps options, any subset of (ensure-ascii B),
; (sort-keys B) and (separators ITEM KEY), for example
; ((ensure-ascii False) (sort-keys True)). Both fail on an integer past 4300
; digits as json.dumps does: (JsonPythonErrorV1 ValueError message prefix),
; prefix being what a streaming json.dump has written by then.
(: json:stringify-python-with (-> %Undefined% %Undefined% Atom))
(= (json:stringify-python-with $value $options)
   (function
     (chain (eval (__cetta_lib_json_stringify $value (JsonPythonV1 $options))) $result
       (return $result))))

(: json:inspect-stringify (-> %Undefined% Atom))
(= (json:inspect-stringify $value)
   (function
     (chain (eval (__cetta_lib_json_stringify_v1 $value)) $result
       (return $result))))

(: json:inspect-stringify-reference (-> %Undefined% Atom))
(= (json:inspect-stringify-reference $value)
   (function
     (chain (eval (__cetta_lib_json_stringify_v1 $value JsonReferenceV1)) $result
       (return $result))))

(: json:from-inspection (-> %Undefined% Atom))
(= (json:from-inspection $value)
   (function
     (chain (eval (__cetta_lib_json_from_inspection_v1 $value)) $result
       (return $result))))

(: json:members (-> %Undefined% Atom))
(= (json:members $object)
   (function
     (chain (eval (__cetta_lib_json_members_v1 $object JsonOrdinaryV1)) $result
       (return $result))))

(: json:get-all (-> %Undefined% %Undefined% Atom))
(= (json:get-all $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_all_v1 $object $key JsonOrdinaryV1)) $result
       (return $result))))

(: json:get-first (-> %Undefined% %Undefined% Atom))
(= (json:get-first $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_first_v1 $object $key JsonOrdinaryV1)) $result
       (return $result))))

(: json:get-last (-> %Undefined% %Undefined% Atom))
(= (json:get-last $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_last_v1 $object $key JsonOrdinaryV1)) $result
       (return $result))))

; First occurrence, or JsonNull when absent (like a dictionary get with a
; null default). get-first/get-last distinguish JsonMissingV1 from JsonNull.
; Lookup checks object/member structure, not unrelated payloads; parse and
; stringify validate the full document at the external-data boundaries.
(: json:get (-> %Undefined% %Undefined% Atom))
(= (json:get $object $key)
   (function
     (chain (eval (__cetta_lib_json_object_get $object $key)) $result
       (return $result))))

(: json:inspect-members (-> %Undefined% Atom))
(= (json:inspect-members $object)
   (function
     (chain (eval (__cetta_lib_json_members_v1 $object)) $result
       (return $result))))

(: json:inspect-get-all (-> %Undefined% %Undefined% Atom))
(= (json:inspect-get-all $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_all_v1 $object $key)) $result
       (return $result))))

(: json:inspect-get-first (-> %Undefined% %Undefined% Atom))
(= (json:inspect-get-first $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_first_v1 $object $key)) $result
       (return $result))))

(: json:inspect-get-last (-> %Undefined% %Undefined% Atom))
(= (json:inspect-get-last $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_last_v1 $object $key)) $result
       (return $result))))

(: json:array-items (-> %Undefined% Atom))
(= (json:array-items $value)
   (function (case $value (((JsonArray $items) (return $items))))))
(: json:object-pairs (-> %Undefined% Atom))
(= (json:object-pairs $value)
   (function (case $value (((JsonObject $pairs) (return $pairs))))))

(: json:to-legacy (-> %Undefined% Atom))
(= (json:to-legacy $value)
   (function
     (chain (eval (__cetta_lib_json_to_legacy_v1 $value)) $result
       (return $result))))

(: json-parse (-> %Undefined% Atom))
(= (json-parse $text)
   (function
     (chain (eval (__cetta_lib_json_parse $text JsonLegacyV1)) $result
       (return $result))))

(: json-stringify (-> %Undefined% Atom))
(= (json-stringify $value)
   (function
     (chain (eval (__cetta_lib_json_stringify $value)) $result
       (return $result))))

(: json-object-get (-> %Undefined% %Undefined% Atom))
(= (json-object-get $object $key)
   (function
     (chain (eval (__cetta_lib_json_object_get $object $key)) $result
       (return $result))))

(= (json-string-value (JsonString $text)) $text)
(= (json-number-value (JsonNumber $text)) $text)
(= (json-bool-value (JsonBool $value)) $value)
(= (json-array-items (JsonArray $items)) $items)
(= (json-object-pairs (JsonObject $pairs)) $pairs)
