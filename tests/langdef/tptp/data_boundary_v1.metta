!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self json)
!(import! &self data_boundary_cases_v1.metta)

!(bind! &mixed-db (new-space))
!(bind! &tagged-db (new-space))
!(boundary:all mixed &mixed-db ((Y (p a))))
!(boundary:all tagged &tagged-db (((var "Y") ((word "p") (word "a")))))
