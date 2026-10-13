!(case (include include_error_source) (((Error $a (Error $b $c)) $c) ((Error $a $b) $b) ($ok loaded)))
!(collapse (match &self (include-error-prefix $x) $x))
!(collapse (match &self (include-error-after $x) $x))
