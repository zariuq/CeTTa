; A declaration made again under another name is the same object: the kernel
; names it by its content.  A lookup by either source name finds it, with the
; later names listed as (also ..); a name no declaration has finds nothing.
; A saved library keeps the names.
!(import! &self megalodon)
(= (named $source)
   (case (mg:check-document () "tests/megalodon/fixtures/other_names.mg" ())
     (((Megalodon.document $theory $authority $verdict $summary $entries) (mg:lookup $theory $source)))))
!(case (mg:check-document () "tests/megalodon/fixtures/other_names.mg" ())
   (((Megalodon.document $theory $authority $verdict $summary $entries) ($verdict $summary))))
!(named "twice")
!(named "again")
!(named "pass")
!(named "pass_again")
!(named "thrice")
!(case (mg:check-document () "tests/megalodon/fixtures/other_names.mg" ())
   (((Megalodon.document $theory $authority $verdict $summary $entries)
     (mg:save-library $theory "runtime/megalodon-test/other_names.mglib"))))
!(let $state (mg:new-theory-state egal) (bind! &loaded $state))
!(mg:load-library (mg:authority (get-state &loaded)) "runtime/megalodon-test/other_names.mglib" (axioms all))
!(mg:lookup (mg:use (get-state &loaded)) "again")
!(mg:lookup (mg:use (get-state &loaded)) "pass_again")
!(mg:lookup (mg:use (get-state &loaded)) "thrice")
; the kernel's name of a declaration, by either of its source names; none
; for a name no declaration has
!(mg:named (mg:use (get-state &loaded)) "twice")
!(mg:named (mg:use (get-state &loaded)) "again")
!(collapse (mg:named (mg:use (get-state &loaded)) "thrice"))
