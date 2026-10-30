; Canonical E2-framed byte records. Fields and encoded results are immutable
; fully supported rank-one U8 tensors. The record stores its count followed by
; every field; zero padding completes the final byte. All framing is charged.
; Unpacking rejects noncanonical headers, truncation and trailing data.
; UTF-8 conversion is explicit and preserves embedded NUL.
(: binary:pack-fields (-> %Undefined% %Undefined%))
(: binary:unpack-fields (-> %Undefined% %Undefined%))
(: binary:packed-length (-> %Undefined% %Undefined%))
; packed-length also accepts (Binary:FieldLengths (length ...)): measure
; exactly the same framing without constructing field contents.
(: binary:utf8 (-> %Undefined% %Undefined%))
(: binary:from-utf8 (-> %Undefined% %Undefined%))
; Minimal big-endian nonnegative machine integers; zero has no payload bytes.
(: binary:unsigned (-> %Undefined% %Undefined%))
(: binary:from-unsigned (-> %Undefined% %Undefined%))
; Symbol names are data: these conversions never invoke a source reader.
(: binary:symbol-utf8 (-> %Undefined% %Undefined%))
(: binary:from-symbol-utf8 (-> %Undefined% %Undefined%))
; Tests the actual scalar representation without constructing a byte vector.
(: binary:unsigned? (-> %Undefined% %Undefined%))
; Physical kind for the portable protocol-value profile. Runtime identities,
; internal representations, nonflat symbol names and unknown facts are refused.
; This classification grants no purity, evaluation or ownership authority.
(: binary:value-kind (-> %Undefined% %Undefined%))
; Extended portable-value profile: signed machine integers, F64 scalars and
; owned tensor values in addition to the BV1 kinds. No authority is conferred.
(: binary:portable-kind (-> %Undefined% %Undefined%))
