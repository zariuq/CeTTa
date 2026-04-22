(= (hide $x) (empty))

!(hide (import! &self oracle_import_syntax_lib))

(foo alpha)
(foo beta)

!(match-single &self (foo $x) $x)
