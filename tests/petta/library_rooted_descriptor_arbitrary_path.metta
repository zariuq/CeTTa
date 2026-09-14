!(register-module! ../support/import_pkg)
!(import! &self (library import_pkg "permissive names/foo..bar"))
!(match &self (permissive-member $value) $value)
