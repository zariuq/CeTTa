!(import! &self "../lib/lib_module.metta")

!(let*
  (($reader (langdef:load-baked-parser "module-core-reader"))
   ($module (module:read-file $reader "regex" "langdef/regex/RegexCore.module"))
   ($closed (langdef:close-parser $reader))
   ($theory (module:assemble $module "RegexDerivatives" LNil LNil OriginViewV1 "example:regex"))
   ($core (module:core $theory "RegexDerivatives"))
   ($authority (langdef:load-language-value $core))
   ($zero (langdef:rewrite-term-at-data $authority "Bool" "Bool" 8 100000
     (LangDef:RelationRows) (rx:nullable (rx:zero))))
   ($epsilon (langdef:rewrite-term-at-data $authority "Bool" "Bool" 8 100000
     (LangDef:RelationRows) (rx:nullable (rx:epsilon))))
   ($bad (langdef:rewrite-term-at-data $authority "Bool" "Bool" 8 100000
     (LangDef:RelationRows) (rx:nullable (NoRegex))))
   ($closed-authority (langdef:close-language $authority)))
  (function (return (RegexNullableV1 $zero $epsilon $bad))))
