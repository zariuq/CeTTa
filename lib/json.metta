; JSON backed by the authored RFC-8259 LanguageDef and prepared ParserPack.
;
; The json:* operations expose the canonical occurrence-preserving V1 value.
; Canonical failures are ordinary JsonFailureV1 evidence so HE, PeTTa, and
; Prime can preserve them without sharing an engine-exception policy.
; The historical json-* operations retain the older JsonObject/JsonPair data
; as an explicit compatibility codec for existing clients.
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
(: __cetta_lib_json_stringify_v1 (-> Atom Atom))
(: __cetta_lib_json_members_v1 (-> Atom Atom))
(: __cetta_lib_json_get_all_v1 (-> Atom String Atom))
(: __cetta_lib_json_get_first_v1 (-> Atom String Atom))
(: __cetta_lib_json_get_last_v1 (-> Atom String Atom))
(: __cetta_lib_json_parse (-> String Atom))
(: __cetta_lib_json_stringify (-> Atom Atom))
(: __cetta_lib_json_object_get (-> Atom String Atom))

(: json:capabilities (-> Atom))
(= (json:capabilities)
   (function
     (chain (eval (__cetta_lib_json_capabilities_v1 ())) $result
       (return $result))))

(: json:parse (-> %Undefined% Atom))
(= (json:parse $text)
   (function
     (chain (eval (__cetta_lib_json_parse_v1 $text)) $result
       (return $result))))

(: json:stringify (-> %Undefined% Atom))
(= (json:stringify $value)
   (function
     (chain (eval (__cetta_lib_json_stringify_v1 $value)) $result
       (return $result))))

(: json:members (-> %Undefined% Atom))
(= (json:members $object)
   (function
     (chain (eval (__cetta_lib_json_members_v1 $object)) $result
       (return $result))))

(: json:get-all (-> %Undefined% %Undefined% Atom))
(= (json:get-all $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_all_v1 $object $key)) $result
       (return $result))))

(: json:get-first (-> %Undefined% %Undefined% Atom))
(= (json:get-first $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_first_v1 $object $key)) $result
       (return $result))))

(: json:get-last (-> %Undefined% %Undefined% Atom))
(= (json:get-last $object $key)
   (function
     (chain (eval (__cetta_lib_json_get_last_v1 $object $key)) $result
       (return $result))))

; Historical GödelClaw compatibility operations.
(: json-parse (-> %Undefined% Atom))
(= (json-parse $text)
   (function
     (chain (eval (__cetta_lib_json_parse $text)) $result
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
