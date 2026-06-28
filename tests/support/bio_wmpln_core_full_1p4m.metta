; Shared realistic WM-PLN workload core over the true full 1.4M raw-loader tier.
; Keep this separate from the post-split checkpoint tier so benchmark rows can
; say exactly which workload they ran.

!(import! &self ./bio_wmpln_workload_rules.metta)

!(import! &bio-wmpln-rb ./bio_wmpln_rulebase_1m.metta)

!(import! &bio-wmpln-kb ./bio_wmpln_data_full_1p4m.metta)
