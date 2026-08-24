!(import! &self (library lib_import))
!(import_prolog_function atomic_list_concat)
!(import_prolog_function split_string)

(= (nested-string-replace $text $needle $replacement)
   (atomic_list_concat
     (split_string $text $needle "")
     $replacement))

!(nested-string-replace
   "agent — first complete line\n⋄⟨Cn:.9 Sp:.4⟩"
   "\n"
   "\\n")
