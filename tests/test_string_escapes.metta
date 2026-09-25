!(import! &self str)

!(assertEqual (str:byte-length "\n") 1)
!(assertEqual (str:byte-length "\t") 1)
!(assertEqual (str:byte-length "\\") 1)
!(assertEqual (str:byte-length "\"") 1)
!(assertEqual (str:find "a\nb" "\n") 1)
