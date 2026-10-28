; TOML 1.1 documents, a superset of TOML 1.0, through the vendored tomlc17.  toml:to-json gives a
; document as JSON text: tables are objects with their keys in document
; order, and dates and times are strings in the form Python's isoformat
; writes them (+00:00 for Z).  toml:parse gives the same as JSON values.  A
; document that is not TOML, and one holding a float JSON cannot carry (an
; infinity or NaN), are refused: (toml:failed KIND DETAIL).
!(import! &self json)

(: __cetta_lib_toml (-> Atom Atom Atom))
(: toml:to-json (-> %Undefined% Atom))
(: toml:parse (-> %Undefined% %Undefined%))

(= (toml:to-json $text)
   (function (chain (eval (__cetta_lib_toml to-json ($text))) $value (return $value))))

(= (toml:parse $text)
   (case (toml:to-json $text)
     (((toml:failed $kind $detail) (toml:failed $kind $detail))
      ($json (json:parse $json)))))
