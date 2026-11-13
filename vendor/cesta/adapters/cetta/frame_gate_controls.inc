/* Live-frame admission controls in the private qualification capture. */
int cesta_frame_gc_controls(void) {
    PrimeEvalStackFrame frame = {0};
    CestaFrameGcAudit audit = {0};
    max_align_t storage;
    void *opaque = &storage;
    if (!cesta_frame_gc_gate(&audit,&frame)) return 1;
#define CESTA_TEST_OPAQUE(FIELD) do { \
    frame.FIELD=opaque; \
    if (cesta_frame_gc_gate(&audit,&frame)) return 2; \
    frame.FIELD=NULL; \
} while (0)
    CESTA_TEST_OPAQUE(control_key);
    CESTA_TEST_OPAQUE(control_bound);
    CESTA_TEST_OPAQUE(control_advice);
    CESTA_TEST_OPAQUE(control_child_bounds);
    CESTA_TEST_OPAQUE(control_nested);
    CESTA_TEST_OPAQUE(control_nested_operation);
    CESTA_TEST_OPAQUE(dialect_source);
    CESTA_TEST_OPAQUE(capsule_search);
    CESTA_TEST_OPAQUE(match_rows);
    CESTA_TEST_OPAQUE(native_service);
#undef CESTA_TEST_OPAQUE
    frame.control_return_advice_length=1;
    if (cesta_frame_gc_gate(&audit,&frame)) return 3;
    frame.control_return_advice_length=0;
    frame.control_return_advice=opaque;
    if (!cesta_frame_gc_gate(&audit,&frame)) return 4;
    frame.control_return_pending=true;
    if (cesta_frame_gc_gate(&audit,&frame)) return 5;
    return 0;
}
