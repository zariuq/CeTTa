; A finite revision delta is validated as a whole before publication.

(= (r0-artifact)
  (compile:rule-package r0
    (rm-package
      (rm-block a source:a (rm-proof-atom a) rm-nil a))))

!(compile:link-package
  (r0-artifact) r1
  (rm-package
    (rm-block b source:b (rm-proof-atom b) rm-nil b)
    (rm-block b source:b-again (rm-proof-atom b-again) rm-nil b)))
