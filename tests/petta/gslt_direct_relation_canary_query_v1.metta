!(import! &self ../../langdef/petta/generated/gslt_direct_relation_canary_v1.metta)

!(collapse
   (gslt:DirectAncestorV1
     (DirectAncestorV1 DirectAliceV1 $descendant)))
!(collapse
   (gslt:DirectParentV1
     (DirectParentV1 DirectMissingV1 $child)))
