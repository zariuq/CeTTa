!(case (import! &self ordered_bad) (((Error $call $why) caught) ($ok unexpected)))
!(collapse (match &self (prefix-private $value) $value))
!(== (mod-space! top) &self)
