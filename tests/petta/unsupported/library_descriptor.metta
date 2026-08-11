!(import! &self (library lib_descriptor_probe))
!(descriptor-probe)

!(register-module! ../../support/import_pkg)
!(import! &self (library import_pkg moduleA))
!(match &self (pkg-root $x) $x)
