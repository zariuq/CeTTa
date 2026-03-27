!(import! &self cverify_compat)

!(assertEqualToResult
  (parse_mm_file "../../hyperon/metamath/metamath-test/tests/core/small/comment1.mm")
  ((Error (parse_mm_file "../../hyperon/metamath/metamath-test/tests/core/small/comment1.mm") nested-comment)))
