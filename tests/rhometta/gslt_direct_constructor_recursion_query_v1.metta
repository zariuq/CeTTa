!(gslt-rho:query:DirectBuildListV1
   QueryDirectBuildListZeroV1
   (DirectBuildListV1 NatZeroV1 $result))

!(gslt-rho:query:DirectBuildListV1
   QueryDirectBuildListOneV1
   (DirectBuildListV1 (NatSuccV1 NatZeroV1) $result))

!(gslt-rho:query:DirectBuildListV1
   QueryDirectBuildListTwoV1
   (DirectBuildListV1
     (NatSuccV1 (NatSuccV1 NatZeroV1)) $result))

!(gslt-rho:query:DirectBuildListV1
   QueryDirectBuildListImpossibleV1
   (DirectBuildListV1 (NatSuccV1 NatZeroV1) DirectBuildNilV1))
