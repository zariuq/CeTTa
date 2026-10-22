!(import! &self "../../../lib/petta/lib_module.metta")

!(let*
  (($reader (langdef:load-baked-parser "module-core-reader"))
   ($source (module:read-file $reader "tensor-support" "langdef/tensor/Support.module"))
   ($fixture (module:read-file $reader "scalar-fixture" "tests/langdef/tensor/ScalarFixture.module"))
   ($closed (langdef:close-parser $reader))
   ($presentation (module:assemble $fixture "Concrete" LNil
      (LCons (ModuleCatalogEntryV1 "TensorSupport" $source) LNil)
      OriginViewV1 "tensor-controls"))
   ($core (module:core $presentation "Concrete"))
   ($language (langdef:load-language-value $core))
   ($missing (langdef:rewrite-term-at-data $language "Presence" "Presence" 8 100000
      (LangDef:RelationRows) (PresenceOf (Absent))))
   ($stored (langdef:rewrite-term-at-data $language "Presence" "Presence" 8 100000
      (LangDef:RelationRows) (PresenceOf (Present (FixtureLeft)))))
   ($fill-missing (langdef:rewrite-term-at-data $language "Scalar" "Scalar" 8 100000
      (LangDef:RelationRows) (Fill (Absent) (FixtureLeft))))
   ($fill-stored (langdef:rewrite-term-at-data $language "Scalar" "Scalar" 8 100000
      (LangDef:RelationRows) (Fill (Present (FixtureRight)) (FixtureLeft))))
   ($inert (langdef:rewrite-term-at-data $language "Cell" "Cell" 8 100000
      (LangDef:RelationRows) (Present (FixtureLeft))))
   ($invalid (langdef:rewrite-term-at-data $language "Presence" "Presence" 8 100000
      (LangDef:RelationRows) (PresenceOf (Present (Absent)))))
   ($closed-language (langdef:close-language $language)))
   (println! (TensorSupport $missing $stored $fill-missing $fill-stored $inert $invalid)))
