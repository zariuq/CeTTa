!(import! &self mork)
!(import! &self ../tests/support/bio_eqtl_mork_query_workload)

!(import! &src data/kb_eqtl_gene_links)
!(space-set-match-backend! &src pathmap-imported)
!(bind! &kb (mork:new-space))
!(match &src $atom (mork:add-atom &kb $atom))
!(run-bio-eqtl-mork-workload &kb)
