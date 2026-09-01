!(register-module! ../support/import_pkg)
!(import! &self (library import_pkg nested/helper))
!(match &self (pkg-helper $value) $value)
