; CeTTa's list syntax over the PeTTa form classes: at the atom level a token no longer
; starts with an opening bracket, and inside a list a token also ends at a bracket; a
; list token starts with a bar only when more follows.  Each rule replaces the base
; rule of the same name or adds a new class.
(gslt-presentation-v1 PeTTaFormListExtensionScalarClassesV1
  (signature)
  (equations)
  (rewrites
    (rule member-petta-token-first-scalar (head (member petta-token-first-scalar ?codepoint)) (body (different ?codepoint (cp 9)) (different ?codepoint (cp 10)) (different ?codepoint (cp 11)) (different ?codepoint (cp 12)) (different ?codepoint (cp 13)) (different ?codepoint (cp 32)) (different ?codepoint (cp 34)) (different ?codepoint (cp 36)) (different ?codepoint (cp 40)) (different ?codepoint (cp 41)) (different ?codepoint (cp 5760)) (different ?codepoint (cp 8192)) (different ?codepoint (cp 8193)) (different ?codepoint (cp 8194)) (different ?codepoint (cp 8195)) (different ?codepoint (cp 8196)) (different ?codepoint (cp 8197)) (different ?codepoint (cp 8198)) (different ?codepoint (cp 8200)) (different ?codepoint (cp 8201)) (different ?codepoint (cp 8202)) (different ?codepoint (cp 8232)) (different ?codepoint (cp 8233)) (different ?codepoint (cp 8287)) (different ?codepoint (cp 12288)) (different ?codepoint (cp 91))))
    (rule member-petta-list-token-first-scalar (head (member petta-list-token-first-scalar ?codepoint)) (body (different ?codepoint (cp 9)) (different ?codepoint (cp 10)) (different ?codepoint (cp 11)) (different ?codepoint (cp 12)) (different ?codepoint (cp 13)) (different ?codepoint (cp 32)) (different ?codepoint (cp 34)) (different ?codepoint (cp 36)) (different ?codepoint (cp 40)) (different ?codepoint (cp 41)) (different ?codepoint (cp 5760)) (different ?codepoint (cp 8192)) (different ?codepoint (cp 8193)) (different ?codepoint (cp 8194)) (different ?codepoint (cp 8195)) (different ?codepoint (cp 8196)) (different ?codepoint (cp 8197)) (different ?codepoint (cp 8198)) (different ?codepoint (cp 8200)) (different ?codepoint (cp 8201)) (different ?codepoint (cp 8202)) (different ?codepoint (cp 8232)) (different ?codepoint (cp 8233)) (different ?codepoint (cp 8287)) (different ?codepoint (cp 12288)) (different ?codepoint (cp 91)) (different ?codepoint (cp 93)) (different ?codepoint (cp 124))))
    (rule member-petta-list-token-scalar (head (member petta-list-token-scalar ?codepoint)) (body (different ?codepoint (cp 9)) (different ?codepoint (cp 10)) (different ?codepoint (cp 13)) (different ?codepoint (cp 32)) (different ?codepoint (cp 40)) (different ?codepoint (cp 41)) (different ?codepoint (cp 91)) (different ?codepoint (cp 93))))
    (rule member-petta-list-token-boundary-scalar-9 (head (member petta-list-token-boundary-scalar (cp 9))) (body))
    (rule member-petta-list-token-boundary-scalar-10 (head (member petta-list-token-boundary-scalar (cp 10))) (body))
    (rule member-petta-list-token-boundary-scalar-13 (head (member petta-list-token-boundary-scalar (cp 13))) (body))
    (rule member-petta-list-token-boundary-scalar-32 (head (member petta-list-token-boundary-scalar (cp 32))) (body))
    (rule member-petta-list-token-boundary-scalar-40 (head (member petta-list-token-boundary-scalar (cp 40))) (body))
    (rule member-petta-list-token-boundary-scalar-41 (head (member petta-list-token-boundary-scalar (cp 41))) (body))
    (rule member-petta-list-token-boundary-scalar-91 (head (member petta-list-token-boundary-scalar (cp 91))) (body))
    (rule member-petta-list-token-boundary-scalar-93 (head (member petta-list-token-boundary-scalar (cp 93))) (body))
  ))
