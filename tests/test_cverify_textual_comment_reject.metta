!(import! &self cverify_compat)

!(assertEqualToResult
  (parse_mm_file "tests/support/metamath/core/small/comment1.mm")
  (parse-unsupported))
