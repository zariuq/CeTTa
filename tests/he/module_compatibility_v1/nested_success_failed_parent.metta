!(case (import! &self nested_parent) (((Error $a $b) caught) ($other unexpected)))
!(case (import! &self nested_child) (((Error $a $b) child-load-error) ($other child-loaded)))
!(collapse (match &self (nested-fact $x) $x))
