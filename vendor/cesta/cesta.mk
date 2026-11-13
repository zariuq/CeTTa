# Include from a project Makefile to build .cesta and .csexp alongside C sources.
CESTA ?= cesta
CESTA_NATIVE_CC ?= $(CC)
CESTA_PRESENTATION ?=
CESTA_PRESENTATION_FLAGS = $(if $(strip $(CESTA_PRESENTATION)),--presentation $(CESTA_PRESENTATION))
CESTA_TOOL_DEPENDENCY ?= $(wildcard $(CESTA))

%.o: %.cesta $(CESTA_TOOL_DEPENDENCY)
	$(CESTA) cc $(CESTA_PRESENTATION_FLAGS) --cc $(CESTA_NATIVE_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%: %.cesta
	$(CESTA) cc $(CESTA_PRESENTATION_FLAGS) --cc $(CESTA_NATIVE_CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

%.o: %.csexp $(CESTA_TOOL_DEPENDENCY)
	$(CESTA) cc --cc $(CESTA_NATIVE_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%: %.csexp
	$(CESTA) cc --cc $(CESTA_NATIVE_CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@
