!(import! &self cverify_compat)

!(assertEqualToResult
  (parse_mm_file "../../hyperon/metamath/metamath-test/tests/core/small/comment1.mm")
  (parse-unsupported))
