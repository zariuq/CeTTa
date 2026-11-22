; A nominal actual type may satisfy a required union through one compatible
; member.  The nominal boundary is preserved: this is elimination from the
; brand into an allowed context, not implicit introduction of a brand.

(: Count (Newtype Number))

(: accept-count-or-string (-> (| String Count) Number))
(= (accept-count-or-string $value) 1)

(: relay-count (-> Count Number))
(= (relay-count $value) (accept-count-or-string $value))
