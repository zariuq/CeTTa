; A publication must advance the revision identity.

(= (r0-artifact)
  (compile:rule-package r0
    (rm-package
      (rm-block a source:a (rm-proof-atom a) rm-nil a))))

!(compile:link-rule
  (r0-artifact) r0
  (rm-block b source:b (rm-proof-atom b) rm-nil b))
