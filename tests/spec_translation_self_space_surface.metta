; Shared default-atomspace surface:
; add-atom &self, match &self, remove-atom &self.
;
; Positive example:
; - translator should preserve this surface and CeTTa/PeTTa should agree.
;
; Negative example:
; - this does not exercise named spaces or get-atoms &self bag contents.

!(add-atom &self (probe-edge a b))
!(add-atom &self (probe-edge b c))
!(match &self (probe-edge $x $y) ($x $y))
!(remove-atom &self (probe-edge a b))
!(match &self (probe-edge $x $y) ($x $y))
