!(import! &self tests/support/petta_import_walk/nested/driver)

!(assertEqual (ancestor-import-value) 17)
