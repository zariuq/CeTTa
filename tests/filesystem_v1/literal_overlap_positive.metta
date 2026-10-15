!(import! &self str)
!(let $overlap (str:find-ascii-uncased "xababababac" "ABABAC")
  (let $literal (str:find-ascii-uncased "file.name" ".")
    (function (return (LiteralOverlap $overlap $literal)))))
