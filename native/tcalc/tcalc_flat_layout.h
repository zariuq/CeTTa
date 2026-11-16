#ifndef TCALC_FLAT_LAYOUT_H
#define TCALC_FLAT_LAYOUT_H

enum {
    FLAT_ROW = 4,
    FLAT_PATTERNS = 24,
    FLAT_CONSTANTS = 32,
    FLAT_GUARDS = 32,
    FLAT_MAKES = 32
};

typedef struct FlatPlan {
    uint32_t constant[FLAT_CONSTANTS], constants;
    uint32_t guard[FLAT_GUARDS], guards;
    uint32_t make[FLAT_MAKES], makes;
    uint32_t made_length[FLAT_MAKES], made_rows;
} FlatPlan;

#endif
