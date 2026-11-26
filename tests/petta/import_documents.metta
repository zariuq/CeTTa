; import! reads a whole document before it runs or adds any of it.  A
; document that does not read raises the reference's syntax error, in the
; context its loader gives an error without one: (context Path "while
; loading MeTTa file"), Path the document's.  Comments go with their line
; ends before the forms are counted, so the unclosed form after two comment
; lines is on line 2.  The gate compares Path from this file's directory.
!(catch (import! &self lib/import_unclosed))
!(catch (import! &self lib/import_stray_close))
!(catch (import! &self lib/import_comment_lines))

; Reading comes first: a document that does not read raises its syntax
; error whatever the destination.
!(catch (import! 42 lib/import_unclosed))

; A document with no atom imports into a space: its queries run, nothing
; is added.
!(import! &self lib/import_empty)
!(import! &import-docs lib/import_queries_only)
!(collapse (get-atoms &import-docs))

; A destination that names no space is refused once the document is found
; and read, before any of it runs, whether or not it holds atoms: add-atom's
; term for its first atom, an expression ([DIFF-27]).
!(catch (import! 42 lib/import_queries_first))
!(catch (import! (foo bar) lib/import_queries_only))
!(catch (import! "str" lib/import_empty))
!(collapse (get-atoms &import-docs))

; Into a space, the document's queries before its first atom run first.
!(import! &import-atoms lib/import_queries_first)
!(collapse (match &import-atoms (imported-atom $x) $x))
