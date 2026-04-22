!(import! &self ../hyperon/PeTTa/lib/lib_builtin_types)

!(assertEqualToResult
  (get-metatype +)
  (Grounded))

!(assertEqualToResult
  (get-type pow-math)
  ((-> Number Number Number)))

!(assertEqualToResult
  (test (get-type min-atom) (-> $a Number))
  (True))

!(assertEqualToResult
  (test (get-type max-atom) (-> $a Number))
  (True))
