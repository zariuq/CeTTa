; PeTTa reflects intrinsic operator atoms as grounded values while ordinary
; symbols and expressions retain their structural metatypes.
!(test (get-metatype +) Grounded)
!(test (get-metatype 1) Grounded)
!(test (get-metatype a) Symbol)
!(test (get-metatype (a b)) Expression)
