; An empty source conjunction succeeds once, so its absence cannot fire.
!(import! &self interaction)
!(bind! &query-space (new-space))
!(collapse (transform &query-space (absent ()) incorrect-empty-source))
!(collapse (transform &query-space (absent (,)) incorrect-empty-source))

; Explicit location disambiguates a literal stored pattern from source syntax.
!(collapse (transform &query-space
   (absent &query-space (let $x 7)) no-literal-let-pattern))
