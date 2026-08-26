; JSON backed by the authored RFC-8259 LanguageDef and prepared ParserPack.
;
; The json:* operations expose the canonical occurrence-preserving V1 value.
; Canonical failures are ordinary JsonFailureV1 evidence so HE, PeTTa, and
; Prime can preserve them without sharing an engine-exception policy.
; The historical json-* operations retain the older JsonObject/JsonPair data
; as an explicit compatibility codec for existing clients.

(= (json:capabilities)
   (function
     (case (__cetta_lib_json_capabilities_v1 ())
       (($result (return $result))))))

(= (json:parse $text)
   (function
     (case (__cetta_lib_json_parse_v1 $text)
       (($result (return $result))))))

(= (json:stringify $value)
   (function
     (case (__cetta_lib_json_stringify_v1 $value)
       (($result (return $result))))))

(= (json:members $object)
   (function
     (case (__cetta_lib_json_members_v1 $object)
       (($result (return $result))))))

(= (json:get-all $object $key)
   (function
     (case (__cetta_lib_json_get_all_v1 $object $key)
       (($result (return $result))))))

(= (json:get-first $object $key)
   (function
     (case (__cetta_lib_json_get_first_v1 $object $key)
       (($result (return $result))))))

(= (json:get-last $object $key)
   (function
     (case (__cetta_lib_json_get_last_v1 $object $key)
       (($result (return $result))))))

; Historical GödelClaw compatibility operations.
(= (json-parse $text)
   (function
     (case (__cetta_lib_json_parse $text)
       (($result (return $result))))))

(= (json-stringify $value)
   (function
     (case (__cetta_lib_json_stringify $value)
       (($result (return $result))))))

(= (json-object-get $object $key)
   (function
     (case (__cetta_lib_json_object_get $object $key)
       (($result (return $result))))))

(= (json-string-value (JsonString $text)) $text)
(= (json-number-value (JsonNumber $text)) $text)
(= (json-bool-value (JsonBool $value)) $value)
(= (json-array-items (JsonArray $items)) $items)
(= (json-object-pairs (JsonObject $pairs)) $pairs)
