; static-import! loads a data file as SWI-PeTTa's importer does, from its
; .qlf, else its .pl, else its .metta converted to both, and its rows join
; the space they name.  The gate runs this twice in a fresh directory: cold,
; converting the .metta, and warm, loading the .qlf that made.
!(import! &self (library lib_import))

; A plain symbol names a space, created by its first rows; each row counts.
!(static-import! facts fixtures/static_import_edges)
!(collapse (match facts (edge $x $y) ($x $y)))
!(collapse (get-atoms facts))

; The rows are static data: adding or removing a row of their length is
; refused as the reference's static predicate refuses it; other lengths
; are free.
!(catch (add-atom facts (edge c d)))
!(catch (remove-atom facts (edge a b)))
!(catch (add-atom facts (foo x)))
!(match facts $p $p)

; Importing a file again changes nothing, whichever space is named.
!(static-import! facts fixtures/static_import_edges)
!(static-import! others fixtures/static_import_edges)
!(collapse (match facts (edge $x $y) ($x $y)))
!(collapse (match others (edge $x $y) ($x $y)))

; Strings, numbers and nested rows keep their values.
!(static-import! things fixtures/static_import_values)
!(collapse (match things (item $s $n $f) ($s $n $f)))
!(collapse (match things (pair $l $r) ($l $r)))

; A second file adds to a multifile predicate (the conversion declares
; Space/3 so); any other predicate a second file defines is redefined, and
; importing the first file again reloads it, as the reference's consult does.
!(static-import! facts fixtures/static_import_more_edges)
!(collapse (match facts (edge $x $y) ($x $y)))
!(static-import! triples fixtures/static_import_triples_a)
!(static-import! triples fixtures/static_import_triples_b)
!(collapse (match triples (edge $x $y $z) ($x $y $z)))
!(static-import! triples fixtures/static_import_triples_a)
!(collapse (match triples (edge $x $y $z) ($x $y $z)))

; A .pl file is consulted as it is, its library directives included; the
; rows are the facts of the predicates it defines, none of a library's.
!(static-import! kb fixtures/static_import_library)
!(collapse (match kb (edge $x $y) ($x $y)))
!(collapse (get-atoms kb))

; use-module! loads a SWI library; an unknown one raises the loader's error.
!(use-module! lists)
!(let (Error $formal $context) (catch (use-module! no_such_library)) caught)

; A file that does not exist raises the reference's existence error.
!(let (Error $formal $context) (catch (static-import! facts fixtures/no_such_file))
    caught)
