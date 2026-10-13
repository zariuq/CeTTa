!(case (import! &self registered_inside) (((Error $a $b) refused-parent) ($ok loaded-parent)))
!(mod-space! registered_leaf)
!(mod-space! registered_provider)
! registered-provider-value
!(import! &self registered_provider)
! registered-provider-value
!(collapse (match &self (registered-leaf $x) $x))
!(bind! registered-root (mod-space! registered_leaf))
!(match registered-root (registered-leaf $x) $x)
