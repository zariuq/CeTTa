; Shared opaque authority.  Import with (import! &self seal).
; Facets, capsules and keys are opaque values: equality, matching, indexing
; and removal use their identity; they print as (Sealer NICK), (Unsealer
; NICK), (Sealed NICK) or (Key), which is display text and grants nothing.
; seal:new hands out a sealer and an unsealer; neither yields the other.
; Sealing captures its payload as written; unsealing returns it unchanged.
; A wrong brand or a non-capsule is no answer; a wrong facet, a missing
; right or a denied policy is a (SealFault REASON) error.  See docs/seal.md.

(: seal:new (-> Atom Atom))
(: __cetta_lib_seal_new (-> Atom Atom))
(= (seal:new $nick)
   (function (chain (eval (__cetta_lib_seal_new $nick)) $result (return $result))))

(: seal:seal (-> %Undefined% Atom Atom))
(: __cetta_lib_seal_seal (-> Atom Atom Atom))
(= (seal:seal $sealer $payload)
   (function (chain (eval (__cetta_lib_seal_seal $sealer $payload)) $result (return $result))))

(: seal:unseal (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_unseal (-> Atom Atom Atom))
(= (seal:unseal $unsealer $capsule)
   (function (chain (eval (__cetta_lib_seal_unseal $unsealer $capsule)) $result (return $result))))

(: seal:attenuate (-> %Undefined% Atom Atom))
(: __cetta_lib_seal_attenuate (-> Atom Atom Atom))
(= (seal:attenuate $facet $rights)
   (function (chain (eval (__cetta_lib_seal_attenuate $facet $rights)) $result (return $result))))

(: seal:new-key (-> Atom))
(: __cetta_lib_seal_new-key (-> Atom Atom))
(= (seal:new-key)
   (function (chain (eval (__cetta_lib_seal_new-key fresh)) $result (return $result))))

(: seal:kind (-> %Undefined% Atom))
(: __cetta_lib_seal_kind (-> Atom Atom))
(= (seal:kind $value)
   (function (chain (eval (__cetta_lib_seal_kind $value)) $result (return $result))))

(: seal:same-brand (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_same-brand (-> Atom Atom Atom))
(= (seal:same-brand $left $right)
   (function (chain (eval (__cetta_lib_seal_same-brand $left $right)) $result (return $result))))

(: seal:nickname (-> %Undefined% Atom))
(: __cetta_lib_seal_nickname (-> Atom Atom))
(= (seal:nickname $value)
   (function (chain (eval (__cetta_lib_seal_nickname $value)) $result (return $result))))

(: seal:portable (-> %Undefined% Atom))
(: __cetta_lib_seal_portable (-> Atom Atom))
(= (seal:portable $value)
   (function (chain (eval (__cetta_lib_seal_portable $value)) $result (return $result))))

(: seal:instantiate (-> %Undefined% Atom))
(: __cetta_lib_seal_instantiate (-> Atom Atom))
(= (seal:instantiate $value)
   (function (chain (eval (__cetta_lib_seal_instantiate $value)) $result (return $result))))

(: seal:new-service (-> Atom Atom))
(: __cetta_lib_seal_new-service (-> Atom Atom))
(= (seal:new-service $nick)
   (function (chain (eval (__cetta_lib_seal_new-service $nick)) $result (return $result))))

; Delegate this facet alone to readers. USER still permits raw opening;
; READER permits only portable snapshots and cannot recover USER or ADMIN.
(: seal:reader (-> %Undefined% Atom))
(: __cetta_lib_seal_reader (-> Atom Atom))
(= (seal:reader $service)
   (function (chain (eval (__cetta_lib_seal_reader $service)) $result (return $result))))

(: seal:try-reader (-> %Undefined% Atom))
(: __cetta_lib_seal_try-reader (-> Atom Atom))
(= (seal:try-reader $service)
   (function (chain (eval (__cetta_lib_seal_try-reader $service)) $result (return $result))))

(: seal:lock (-> %Undefined% %Undefined% Atom Atom))
(: __cetta_lib_seal_lock (-> Atom Atom Atom Atom))
(= (seal:lock $sealer $key $payload)
   (function (chain (eval (__cetta_lib_seal_lock $sealer $key $payload)) $result (return $result))))

(: seal:grant (-> %Undefined% %Undefined% Atom Atom))
(: __cetta_lib_seal_grant (-> Atom Atom Atom Atom))
(= (seal:grant $admin $key $mode)
   (function (chain (eval (__cetta_lib_seal_grant $admin $key $mode)) $result (return $result))))

(: seal:revoke (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_revoke (-> Atom Atom Atom))
(= (seal:revoke $admin $key)
   (function (chain (eval (__cetta_lib_seal_revoke $admin $key)) $result (return $result))))

(: seal:open (-> %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_open (-> Atom Atom Atom Atom))
(= (seal:open $service $capsule $key)
   (function (chain (eval (__cetta_lib_seal_open $service $capsule $key)) $result (return $result))))

(: seal:read-space (-> %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_read-space (-> Atom Atom Atom Atom))
(= (seal:read-space $service $capsule $key)
   (function (chain (eval (__cetta_lib_seal_read-space $service $capsule $key)) $result (return $result))))

; try- forms: (seal:try-OP ARGS ..) is (seal:OP ARGS ..), except that a fault is
; the value (Error CALL (SealFault REASON)) rather than a raised error, so
; a program can branch on it.  A recognition miss is still no answer.

(: seal:try-seal (-> %Undefined% Atom Atom))
(: __cetta_lib_seal_try-seal (-> Atom Atom Atom))
(= (seal:try-seal $sealer $payload)
   (function (chain (eval (__cetta_lib_seal_try-seal $sealer $payload)) $result (return $result))))

(: seal:try-unseal (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_try-unseal (-> Atom Atom Atom))
(= (seal:try-unseal $unsealer $capsule)
   (function (chain (eval (__cetta_lib_seal_try-unseal $unsealer $capsule)) $result (return $result))))

(: seal:try-attenuate (-> %Undefined% Atom Atom))
(: __cetta_lib_seal_try-attenuate (-> Atom Atom Atom))
(= (seal:try-attenuate $facet $rights)
   (function (chain (eval (__cetta_lib_seal_try-attenuate $facet $rights)) $result (return $result))))

(: seal:try-portable (-> %Undefined% Atom))
(: __cetta_lib_seal_try-portable (-> Atom Atom))
(= (seal:try-portable $value)
   (function (chain (eval (__cetta_lib_seal_try-portable $value)) $result (return $result))))

(: seal:try-lock (-> %Undefined% %Undefined% Atom Atom))
(: __cetta_lib_seal_try-lock (-> Atom Atom Atom Atom))
(= (seal:try-lock $sealer $key $payload)
   (function (chain (eval (__cetta_lib_seal_try-lock $sealer $key $payload)) $result (return $result))))

(: seal:try-grant (-> %Undefined% %Undefined% Atom Atom))
(: __cetta_lib_seal_try-grant (-> Atom Atom Atom Atom))
(= (seal:try-grant $admin $key $mode)
   (function (chain (eval (__cetta_lib_seal_try-grant $admin $key $mode)) $result (return $result))))

(: seal:try-revoke (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_try-revoke (-> Atom Atom Atom))
(= (seal:try-revoke $admin $key)
   (function (chain (eval (__cetta_lib_seal_try-revoke $admin $key)) $result (return $result))))

(: seal:try-open (-> %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_try-open (-> Atom Atom Atom Atom))
(= (seal:try-open $service $capsule $key)
   (function (chain (eval (__cetta_lib_seal_try-open $service $capsule $key)) $result (return $result))))

(: seal:try-read-space (-> %Undefined% %Undefined% %Undefined% Atom))
(: __cetta_lib_seal_try-read-space (-> Atom Atom Atom Atom))
(= (seal:try-read-space $service $capsule $key)
   (function (chain (eval (__cetta_lib_seal_try-read-space $service $capsule $key)) $result (return $result))))
