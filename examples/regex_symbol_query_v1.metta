!(import! &self re)

(RegexNamedSymbolV1 trie-name:alpha)
(RegexNamedSymbolV1 trie-name:beta)
(RegexNamedSymbolV1 trie-name:λ)

; The query domain is the finite interned-name snapshot, not space rows.
!(let $result (re:symbol-query-with (re:pattern "trie-name:(?:alpha|λ)") Full)
   (case $result
     (((re:symbols $names $snapshot) (RegexSymbolNamesV1 $names))
      ($other (RegexSymbolUnexpectedV1 $other)))))

; Prefix and search are ordinary, explicitly selectable observations.
!(let $result (re:symbol-query-with (re:pattern "trie-name:a") Prefix)
   (case $result
     (((re:symbols $names $snapshot) (RegexSymbolPrefixV1 $names))
      ($other (RegexSymbolUnexpectedV1 $other)))))

; No matching text name gives a completed empty list.
!(let $result (re:symbol-query-with (re:pattern "trie-name:never") Full)
   (case $result
     (((re:symbols $names $snapshot) (RegexSymbolAbsentV1 $names))
      ($other (RegexSymbolUnexpectedV1 $other)))))

; Nested observations retain their own matching semantics through filtering.
!(let $result (re:symbol-query-with (re:pattern "trie-name:alpha(?=$)") Full)
   (case $result
     (((re:symbols $names $snapshot) (RegexSymbolLookV1 $names))
      ($other (RegexSymbolUnexpectedV1 $other)))))
