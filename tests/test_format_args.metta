; HE string.rs + stdlib docs: format-args replaces {} placeholders with the
; atom_to_string rendering of atoms from the second expression. Grounded string
; atoms contribute their raw contents, while other atoms use ordinary display.

!(format-args "{} + {}" (A B))
!(format-args "{} {}" ("A" B))
!(format-args "{}" ((A B)))
!(format-args "{} {}" (A))
!(format-args "{}" (A B))
