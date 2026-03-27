!(import! &self pverify-utils)

!(add_c &kb "formula")
!(add_v &kb "x" 0)
!(add_v &kb "y" 0)
!(add_f &kb "xf" "formula" "x" 0)
!(add_f &kb "yf" "formula" "y" 0)
!(add_dv_pair_if_fresh &kb "x" "y" 0)

!(bind! &subst_dv (new-space))
!(add-atom &subst_dv ("x" ("x")))
!(add-atom &subst_dv ("y" ("x")))

!(assertEqual (lookup_d &kb "x" "y") True)
!(assertEqual (find_vars &kb ("x")) ("x"))
!(assertEqual
   (select (make_assertion &kb ("|-" "x" "y")))
   ((DVars (("x" "y"))) (FHyps (("formula" "x") ("formula" "y"))) (EHyps ()) (Statement ("|-" "x" "y"))))
!(assertEqual
   (check_dvs &kb &subst_dv (("x" "y")))
   (Error ("x" "x") "Disjoint variable violation - same variable."))
