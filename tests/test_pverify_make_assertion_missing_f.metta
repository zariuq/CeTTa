!(import! &self pverify-utils)

!(add_c &kb "formula")
!(add_c &kb "A.")
!(add_c &kb "setvar")
!(add_v &kb "ph" 0)
!(add_v &kb "x" 0)
!(add_f &kb "wph" "formula" "ph" 0)

!(assertEqual
   (select (make_assertion &kb ("formula" "A." "x" "ph")))
   ((DVars ()) (FHyps ()) (EHyps ()) (Statement ("formula" "A." "x" "ph"))))
