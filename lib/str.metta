; Strings are bytes plus a length; a string may hold NUL.  Byte operations
; are named as such and take any bytes.  Codepoint operations read strict
; UTF-8 and refuse malformed input, naming the byte offset of the first
; invalid sequence; nothing indexes characters in O(1).  A text argument is a
; string or a symbol's spelling.  Conversions read their whole input and
; refuse anything else.  Prepared equation runners call these operations
; under the same names.

(= (str:byte-length $text)
   (function
     (case (__cetta_lib_str byte-length $text)
       (($result (return $result))))))

; The bytes from start up to end; an offset outside the text is refused.
(= (str:byte-slice $text $start $end)
   (function
     (case (__cetta_lib_str byte-slice $text $start $end)
       (($result (return $result))))))

(= (str:concat $lhs $rhs)
   (function
     (case (__cetta_lib_str concat $lhs $rhs)
       (($result (return $result))))))

(= (str:join $separator $parts)
   (function
     (case (__cetta_lib_str join $separator $parts)
       (($result (return $result))))))

(= (str:split $separator $text)
   (function
     (case (__cetta_lib_str split $separator $text)
       (($result (return $result))))))

(= (str:split-whitespace $text)
   (function
     (case (__cetta_lib_str split-whitespace $text)
       (($result (return $result))))))

(= (str:find $text $needle)
   (function
     (case (__cetta_lib_str find $text $needle)
       (($result (return $result))))))

(= (str:starts-with $text $prefix)
   (function
     (case (__cetta_lib_str starts-with $text $prefix)
       (($result (return $result))))))

(= (str:ends-with $text $suffix)
   (function
     (case (__cetta_lib_str ends-with $text $suffix)
       (($result (return $result))))))

(= (str:trim $text)
   (function
     (case (__cetta_lib_str trim $text)
       (($result (return $result))))))

; The SHA-256 digest of the text's bytes, as 64 lowercase hex digits.
(= (str:sha256 $text)
   (function
     (case (__cetta_lib_str sha256 $text)
       (($result (return $result))))))

; -1, 0 or 1 by byte order, a proper prefix first.
(= (str:compare $lhs $rhs)
   (function
     (case (__cetta_lib_str compare $lhs $rhs)
       (($result (return $result))))))

(= (str:codepoint-count $text)
   (function
     (case (__cetta_lib_str codepoint-count $text)
       (($result (return $result))))))

; The codepoints of a text as a list value of one-codepoint strings, so that
; [$c | $rest] matches over its characters.
(= (str:codepoints $text)
   (function
     (case (__cetta_lib_str codepoints $text)
       (($result (return $result))))))

; SipHash-1-3 (CPython's variant: the lanes xor'ed at the end) under the key
; (k0, k1) of the text's code points written in the narrowest fixed width
; that holds them all (1, 2 or 4 bytes, little-endian): the hash CPython
; gives a str under that key (with PYTHONHASHSEED=0 the key is 0 0).  An
; integer from 0 to 2^64 - 1; the empty text hashes to 0.
(= (str:siphash13 $k0 $k1 $text)
   (function
     (case (__cetta_lib_str siphash13 $k0 $k1 $text)
       (($result (return $result))))))

; The bytes of a text, as integers from 0 to 255; any bytes.
(= (str:byte-values $text)
   (function
     (case (__cetta_lib_str byte-values $text)
       (($result (return $result))))))

; The scalar values of a text's codepoints, as integers.
(= (str:codepoint-values $text)
   (function
     (case (__cetta_lib_str codepoint-values $text)
       (($result (return $result))))))

; One string from a list of codepoints, each a one-codepoint string or an
; integer scalar value.
(= (str:from-codepoints $codepoints)
   (function
     (case (__cetta_lib_str from-codepoints $codepoints)
       (($result (return $result))))))

; One string from integer code points up to U+10FFFF, surrogates included,
; each in UTF-8's bit pattern (generalized UTF-8): the strings of a language
; whose text is code point sequences (Python's str); UTF-8 exactly when it
; holds no surrogate.
(= (str:from-codepoints-generalized $codepoints)
   (function
     (case (__cetta_lib_str from-codepoints-generalized $codepoints)
       (($result (return $result))))))

; Character classes over codepoints, those of the C locale: alpha, digit,
; alnum, lower, upper, space, blank, punct, print, graph, cntrl, xdigit, word
; (alnum or _) and ascii.  A codepoint beyond ASCII is in none of them.
; Whether every codepoint of a text is in a class (the empty text is), and
; whether its first codepoint is (the empty text has none).
(= (str:is-all-in $class $text)
   (function
     (case (__cetta_lib_str is-all-in $class $text)
       (($result (return $result))))))

(= (str:is-initial-in $class $text)
   (function
     (case (__cetta_lib_str is-initial-in $class $text)
       (($result (return $result))))))

; Spelling tests: letters and digits only; a lower-case or an upper-case
; initial; and a lower word or an upper word, such an initial followed by word
; characters.
(= (str:is-alphanumeric $text)
   (function
     (case (__cetta_lib_str is-alphanumeric $text)
       (($result (return $result))))))

(= (str:is-lower-initial $text)
   (function
     (case (__cetta_lib_str is-lower-initial $text)
       (($result (return $result))))))

(= (str:is-upper-initial $text)
   (function
     (case (__cetta_lib_str is-upper-initial $text)
       (($result (return $result))))))

(= (str:is-lower-word $text)
   (function
     (case (__cetta_lib_str is-lower-word $text)
       (($result (return $result))))))

(= (str:is-upper-word $text)
   (function
     (case (__cetta_lib_str is-upper-word $text)
       (($result (return $result))))))

; A symbol from a string: nonempty UTF-8 without NUL.
(= (str:string->symbol $text)
   (function
     (case (__cetta_lib_str string->symbol $text)
       (($result (return $result))))))

(= (str:symbol->string $symbol)
   (function
     (case (__cetta_lib_str symbol->string $symbol)
       (($result (return $result))))))

; A number from its whole decimal spelling: an integer of any size, or a
; float with a point or an exponent, rounded to the nearest double; a float
; beyond the finite doubles is refused.
(= (str:string->number $text)
   (function
     (case (__cetta_lib_str string->number $text)
       (($result (return $result))))))

(= (str:number->string $number)
   (function
     (case (__cetta_lib_str number->string $number)
       (($result (return $result))))))

; A string set is built once from a list of strings and can be bound and
; shared; membership searches it without rebuilding it.
(= (str:set $strings)
   (function
     (case (__cetta_lib_str set $strings)
       (($result (return $result))))))

(= (str:is-set-member $set $text)
   (function
     (case (__cetta_lib_str is-set-member $set $text)
       (($result (return $result))))))

; The quoting schemes are metta, json, tptp-single and tptp-double: the body
; of a MeTTa string literal, of a JSON string, of a TPTP quoted atom and of a
; TPTP distinct object.
(= (str:escape $scheme $text)
   (function
     (case (__cetta_lib_str escape $scheme $text)
       (($result (return $result))))))

(= (str:unescape $scheme $text)
   (function
     (case (__cetta_lib_str unescape $scheme $text)
       (($result (return $result))))))

; Pure streaming states are ordinary data. Compute helpers in explicit let
; bindings before handing their state to another operation. The native uses
; the same operator table as compiled consumers, with an inert operand tuple.
; Refusals are (StrRefusedV1 call detail) data, not raised host control.
(: __cetta_lib_str_value (-> Atom Atom Atom))
(: str:utf8-new (-> %Undefined% Atom))
(: str:utf8-feed (-> Atom %Undefined% %Undefined% Atom))
(: str:line-new (-> Atom))
(: str:line-segments (-> %Undefined% %Undefined% Atom))
(: str:collect-lines (-> Atom Atom %Undefined% Atom))
(: str:feed-lines (-> Atom %Undefined% %Undefined% Atom))
(: str:advance-lines (-> Atom %Undefined% %Undefined% Atom))
(: str:find-ascii-uncased (-> %Undefined% %Undefined% Atom))

(= (str:utf8-new $policy)
   (function (chain (eval (__cetta_lib_str_value utf8-new ($policy))) $result
     (return $result))))

; final requests a pending-scalar flush; it does not certify OS EOF. An empty chunk
; with final=False is not end. strict refuses malformed input; ignore erases
; malformed bytes explicitly. Returned text is valid UTF-8, NUL included.
(= (str:utf8-feed $state $bytes $final)
   (function (chain (eval (__cetta_lib_str_value utf8-feed ($state $bytes $final))) $result
     (return $result))))

(= (str:line-new)
   (function (chain (eval (__cetta_lib_str_value line-new ())) $result
     (return $result))))

; Unicode line boundaries include CRLF as one boundary, even across chunks.
(= (str:line-segments $text $preceding-cr)
   (function (chain (eval (__cetta_lib_str_value line-segments ($text $preceding-cr))) $result
     (return $result))))

; Keep reversed fragments until each line completes, then copy it once.
(= (str:collect-lines $buffer $segments $final)
   (function (chain (eval (__cetta_lib_str_value collect-lines ($buffer $segments $final))) $result
     (return $result))))

; Fused text preparation. feed-lines publishes completed lines; advance-lines
; updates the same counter/CR/pending-prefix state without assembling those
; lines. Both retain the unfinished line for later value observation.
(= (str:feed-lines $buffer $text $final)
   (function (chain (eval (__cetta_lib_str_value feed-lines ($buffer $text $final))) $result
     (return $result))))
(= (str:advance-lines $buffer $text $final)
   (function (chain (eval (__cetta_lib_str_value advance-lines ($buffer $text $final))) $result
     (return $result))))

; Literal ASCII patterns with Unicode-uncased ASCII equivalence. The text may
; hold arbitrary bytes; malformed bytes are opaque, never silently erased.
; Returns a byte offset or TextNotFound. This is not a regex operation.
(= (str:find-ascii-uncased $text $needle)
   (function (chain (eval (__cetta_lib_str_value find-ascii-uncased ($text $needle))) $result
     (return $result))))

; Unicode (the generated data of Unicode 15.1.0): (str:case-map KIND TEXT)
; maps each code point by its full case mapping of KIND, "lower", "upper",
; "title" or "fold" (lowering a capital sigma by the Final_Sigma condition);
; (str:props-all MASK TEXT) and (str:props-any MASK TEXT) ask whether every or
; some code point has a property of the bit set MASK; (str:code-props CP) is
; a code point's bit set (bits 0-29 its general category, then Lowercase,
; Uppercase, Cased, Case_Ignorable, XID_Start, XID_Continue, Numeric_Type
; Decimal, Digit and Numeric, bidirectional class WS, B and S)
(= (str:case-map $kind $text)
   (function
     (case (__cetta_lib_str case-map $kind $text)
       (($result (return $result))))))
(= (str:props-all $mask $text)
   (function
     (case (__cetta_lib_str props-all $mask $text)
       (($result (return $result))))))
(= (str:props-any $mask $text)
   (function
     (case (__cetta_lib_str props-any $mask $text)
       (($result (return $result))))))
(= (str:code-props $cp)
   (function
     (case (__cetta_lib_str code-props $cp)
       (($result (return $result))))))
; (str:trim-props MASK SIDE TEXT) drops the code points with a property of
; MASK from TEXT's start (SIDE "left"), end ("right") or "both";
; (str:trim-chars CHARS SIDE TEXT) the code points of CHARS; (str:split-props
; MASK MAXSPLIT TEXT) gives the runs of code points without a property of
; MASK, at most MAXSPLIT splits (negative: no limit), the rest one item;
; (str:rsplit-props ...) splits from the end
(= (str:trim-props $mask $side $text)
   (function
     (case (__cetta_lib_str trim-props $mask $side $text)
       (($result (return $result))))))
(= (str:trim-chars $chars $side $text)
   (function
     (case (__cetta_lib_str trim-chars $chars $side $text)
       (($result (return $result))))))
(= (str:split-props $mask $limit $text)
   (function
     (case (__cetta_lib_str split-props $mask $limit $text)
       (($result (return $result))))))
(= (str:rsplit-props $mask $limit $text)
   (function
     (case (__cetta_lib_str rsplit-props $mask $limit $text)
       (($result (return $result))))))
; (str:find-index SUB TEXT) is the code point index of SUB's first
; occurrence in TEXT, -1 when none; (str:rfind-index SUB TEXT) of its last
(= (str:find-index $sub $text)
   (function
     (case (__cetta_lib_str find-index $sub $text)
       (($result (return $result))))))
(= (str:rfind-index $sub $text)
   (function
     (case (__cetta_lib_str rfind-index $sub $text)
       (($result (return $result))))))
; (str:from-bytes VALUES) is the string of these byte values (0 to 255),
; whatever they encode
(= (str:from-bytes $values)
   (function
     (case (__cetta_lib_str from-bytes $values)
       (($result (return $result))))))
