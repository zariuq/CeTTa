# Source table SHA256 at import: 62eb7c67ecb5bef7fef354225339cf917cb0f56012c3613a80452ea528613b20
# The shared axes of the transform step as MeTTa atoms, one per table, for
# lib/step_settings.metta: generated from lib/transform_axes.data, spellings and
# table names as symbols.  Axes also names each axis's table of values.  The
# HE lane reads lib/, the PeTTa lane lib/petta/: one content.
TRANSFORM_AXES_METTA = lib/transform_axes.metta lib/petta/transform_axes.metta
TRANSFORM_AXES_TABLES = Steps Asks Stores Matches Removals Conjunctions BranchTimings Commas Dialects
TRANSFORM_AXES_SYMBOLS = sed 's/"\([A-Za-z][A-Za-z0-9-]*\)"/\1/g'
.PHONY: transform-axes-metta
transform-axes-metta: lib/transform_axes.data $(CESTA)
	@set -eu; out=$(firstword $(TRANSFORM_AXES_METTA)); \
	echo '; generated from lib/transform_axes.data by make transform-axes-metta, do not edit' > "$$out"; \
	for t in $(TRANSFORM_AXES_TABLES); do \
		$(CESTA) tables rows lib/transform_axes.data $$t spelling | $(TRANSFORM_AXES_SYMBOLS) >> "$$out"; \
	done; \
	$(CESTA) tables rows lib/transform_axes.data Axes spelling values | $(TRANSFORM_AXES_SYMBOLS) >> "$$out"; \
	$(CESTA) tables rows lib/transform_axes.data Presets dialect axis value | $(TRANSFORM_AXES_SYMBOLS) >> "$$out"; \
	for copy in $(wordlist 2,$(words $(TRANSFORM_AXES_METTA)),$(TRANSFORM_AXES_METTA)); do cp "$$out" "$$copy"; done; \
	echo "wrote $(TRANSFORM_AXES_METTA)"
