; Megalodon source read by the syntax presentations of langdef/megalodon/,
; compiled to C when CeTTa is built: a fixture's tokens and its items (the
; end of input ends the document inside an item, as in Megalodon), a file
; Megalodon refuses, with its message, and a file that cannot be read.
!(import! &self megalodon)
!(mg:gslt-tokens "tests/megalodon/source_reader/eof_inside_item.mg")
!(mg:gslt-items () "tests/megalodon/source_reader/eof_inside_item.mg")
!(case (mg:try-gslt-items () "tests/megalodon/source_reader/neg_unmatched_paren.mg")
   (((Error $call $why) $why) ($reading $reading)))
!(case (mg:try-gslt-tokens "tests/megalodon/source_reader/no_such_file.mg")
   (((Error $call $why) $why) ($reading $reading)))
