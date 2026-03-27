!(import! &self str)

!(assertEqual (str-length "alpha") 5)
!(assertEqual (str-split-whitespace "  a\tb\nc  ") ("a" "b" "c"))
!(assertEqual (str-trim "  hi  ") "hi")
