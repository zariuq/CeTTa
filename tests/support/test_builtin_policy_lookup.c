#include "../../src/session.c"

static const CettaBuiltinPolicy *linear_policy(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(CETTA_BUILTIN_POLICIES) /
                                  sizeof(CETTA_BUILTIN_POLICIES[0]); i++) {
        if (strcmp(name, CETTA_BUILTIN_POLICIES[i].name) == 0)
            return &CETTA_BUILTIN_POLICIES[i];
    }
    return NULL;
}

int main(void) {
    if (cetta_builtin_policy_lookup(NULL) || cetta_builtin_policy_lookup(""))
        return 1;
    size_t count = sizeof(CETTA_BUILTIN_POLICIES) /
                   sizeof(CETTA_BUILTIN_POLICIES[0]);
    for (size_t i = 0; i < count; i++) {
        const char *name = CETTA_BUILTIN_POLICIES[i].name;
        if ((i && strcmp(CETTA_BUILTIN_POLICIES[i - 1u].name, name) >= 0) ||
            cetta_builtin_policy_lookup(name) != &CETTA_BUILTIN_POLICIES[i])
            return 1;
        char neighbor[128];
        size_t length = strlen(name);
        if (length + 2u > sizeof(neighbor)) return 1;
        memcpy(neighbor, name, length + 1u);
        /* Prefixes, extensions and one-byte changes exercise both sides of
         * each boundary, including neighbors that are also policy names. */
        for (size_t position = 0; position <= length; position++) {
            for (unsigned byte = 0; byte < 256u; byte++) {
                neighbor[position] = (char)byte;
                neighbor[length + 1u] = '\0';
                if (cetta_builtin_policy_lookup(neighbor) != linear_policy(neighbor))
                    return 1;
            }
            neighbor[position] = name[position];
        }
    }
    /* Positions are the compact names a symbol keeps for its policy. */
    if (cetta_builtin_policy_position(NULL) != 0u ||
        cetta_builtin_policy_at_position(0u) ||
        cetta_builtin_policy_at_position((uint32_t)count + 1u) ||
        count + 1u > CETTA_BUILTIN_POLICY_POSITIONS)
        return 1;
    for (size_t i = 0; i < count; i++) {
        const CettaBuiltinPolicy *policy = &CETTA_BUILTIN_POLICIES[i];
        uint32_t position = cetta_builtin_policy_position(policy);
        if (position != (uint32_t)i + 1u ||
            cetta_builtin_policy_at_position(position) != policy ||
            cetta_language_allows_builtin_policy(
                CETTA_LANGUAGE_PETTA, NULL, policy) !=
                cetta_language_allows_builtin(
                    CETTA_LANGUAGE_PETTA, NULL, policy->name))
            return 1;
    }
    if (!cetta_language_allows_builtin_policy(CETTA_LANGUAGE_PETTA, NULL, NULL))
        return 1;
    puts("PASS: builtin policy lookup preserves every policy and lexical boundary");
    puts("PASS: builtin policy positions round-trip and decide as the names do");
    return 0;
}
