!(bind! &rel (new-space))
!(import! &rel support/import_rel/root.metta)
!(match &rel (rel-root $x) $x)
!(match &rel (rel-helper $x) $x)
