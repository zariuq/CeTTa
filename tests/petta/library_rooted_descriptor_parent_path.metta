!(register-module! ../support/import_pkg)
!(import! &self (library import_pkg ../import_pkg_deep/moduleA))
!(match &self (pkg-deep-root $value) $value)
