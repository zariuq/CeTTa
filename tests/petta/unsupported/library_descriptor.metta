!(import! &self (library lib_he))
!(if-equal descriptor descriptor equal unequal)

!(register-module! ../../support/import_pkg)
!(import! &self (library import_pkg moduleA))
!(match &self (pkg-root $x) $x)
