!(import! &self str)

!(assertEqual (str-length "\n") 1)
!(assertEqual (str-length "\t") 1)
!(assertEqual (str-length "\\") 1)
!(assertEqual (str-length "\"") 1)
!(assertEqual (str-find "a\nb" "\n") 1)
