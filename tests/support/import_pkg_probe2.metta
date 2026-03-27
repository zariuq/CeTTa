!(register-module! ./support/import_pkg)
!(import! &db import_pkg:moduleA)
!(match &db (pkg-root $x) $x)
!(match &db (pkg-helper $x) $x)
