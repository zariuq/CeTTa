!(import! &self cverify_compat)

!(assertEqualToResult
  (parse_mm_file "tests/support/metamath/tiny_cverify_compressed.mm")
  (parse-unsupported))
