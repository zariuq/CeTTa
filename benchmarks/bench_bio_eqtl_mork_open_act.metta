!(import! &self mork)
!(import! &self ../tests/support/bio_eqtl_mork_query_workload)

!(bind! &kb (mork:open-act "../runtime/bench_eqtl_for_mining.act"))
!(run-bio-eqtl-mork-workload &kb)
