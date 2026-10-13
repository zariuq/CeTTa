!(import! &self named_parent_a)
!(import! &self named_parent_b)
!(collapse (match &self (named-scope $x) $x))
