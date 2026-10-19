!(import! &self str)
!(let $first (str:find-ascii-uncased "İıſK" "I")
  (let $last (str:find-ascii-uncased "İıſK" "K")
    (function (return (LiteralUnicode $first $last)))))
