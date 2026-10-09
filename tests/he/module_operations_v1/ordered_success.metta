!(import! &self ordered_parent)
!(match &self (parent-value $value) $value)
!(match &self (child-value $value) $value)
!(== (mod-space! top) &self)
