; Duplicate block identities are rejected before an artifact is published.

!(compile:rule-package duplicate
  (rm-package
    (rm-block same source:a (rm-proof-atom a) rm-nil a)
    (rm-block same source:b (rm-proof-atom b) rm-nil b)))
