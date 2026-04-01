; Cross-runtime conformance: import! into a fresh named space
!(import! &kb support/tiny_kb_conformance)
!(println! "=== match &kb ===")
!(match &kb (fact $x) $x)
!(println! "=== match &self (should be empty) ===")
!(match &self (fact $x) $x)
