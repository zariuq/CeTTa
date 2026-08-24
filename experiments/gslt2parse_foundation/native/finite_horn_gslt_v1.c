#include "finite_horn_gslt_v1.h"

#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FHGSLT_MAX_DEPTH 4096u

typedef enum {
    NODE_SYMBOL,
    NODE_VARIABLE,
    NODE_STRING,
    NODE_INTEGER,
    NODE_LIST
} NodeKind;

typedef struct Node Node;

struct Node {
    NodeKind kind;
    uint8_t *text;
    size_t text_len;
    Node **items;
    size_t item_count;
    size_t item_cap;
};

typedef struct {
    const uint8_t *bytes;
    size_t len;
} Text;

typedef struct {
    Text name;
    size_t arity;
} OperatorDecl;

typedef struct {
    Text name;
    Node *head;
    Node **body;
    size_t body_count;
} RuleDecl;

typedef struct {
    Text name;
    OperatorDecl *operators;
    size_t operator_count;
    RuleDecl *rules;
    size_t rule_count;
    Node *root;
    char *source;
} Presentation;

static int compare_rule_pointer(const void *left, const void *right);
static int compare_presentation_pointer(const void *left,
                                        const void *right);

struct FHGSLTPackage {
    Presentation *presentations;
    size_t presentation_count;
};

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
} ByteBuffer;

typedef struct {
    const uint8_t *bytes;
    size_t len;
    size_t pos;
    const char *source;
    char *error;
    size_t error_cap;
} Parser;

static bool set_error(char *error, size_t error_cap, const char *format, ...) {
    if (error != NULL && error_cap > 0u) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(error, error_cap, format, args);
        va_end(args);
    }
    return false;
}

static bool parser_error(Parser *parser, const char *format, ...) {
    if (parser->error != NULL && parser->error_cap > 0u) {
        size_t used = 0u;
        int prefix = snprintf(parser->error,
                              parser->error_cap,
                              "%s: ",
                              parser->source != NULL ? parser->source : "<text>");
        if (prefix > 0) {
            used = (size_t)prefix;
            if (used >= parser->error_cap)
                used = parser->error_cap - 1u;
        }
        va_list args;
        va_start(args, format);
        (void)vsnprintf(parser->error + used,
                        parser->error_cap - used,
                        format,
                        args);
        va_end(args);
    }
    return false;
}

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (right > SIZE_MAX - left)
        return false;
    *out = left + right;
    return true;
}

static bool buffer_reserve(ByteBuffer *buffer, size_t extra) {
    size_t required;
    if (!checked_add(buffer->len, extra, &required))
        return false;
    if (required <= buffer->cap)
        return true;
    size_t next = buffer->cap == 0u ? 128u : buffer->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    uint8_t *grown = (uint8_t *)realloc(buffer->bytes, next);
    if (grown == NULL)
        return false;
    buffer->bytes = grown;
    buffer->cap = next;
    return true;
}

static bool buffer_append(ByteBuffer *buffer,
                          const uint8_t *bytes,
                          size_t len) {
    if (!buffer_reserve(buffer, len))
        return false;
    if (len > 0u)
        memcpy(buffer->bytes + buffer->len, bytes, len);
    buffer->len += len;
    return true;
}

static bool buffer_byte(ByteBuffer *buffer, uint8_t byte) {
    return buffer_append(buffer, &byte, 1u);
}

static bool buffer_literal(ByteBuffer *buffer, const char *literal) {
    return buffer_append(buffer,
                         (const uint8_t *)literal,
                         strlen(literal));
}

static void buffer_destroy(ByteBuffer *buffer) {
    free(buffer->bytes);
    memset(buffer, 0, sizeof(*buffer));
}

static bool utf8_decode(const uint8_t *bytes,
                        size_t len,
                        size_t pos,
                        uint32_t *codepoint,
                        size_t *width) {
    if (pos >= len)
        return false;
    uint8_t first = bytes[pos];
    if (first <= 0x7fu) {
        *codepoint = first;
        *width = 1u;
        return true;
    }
    uint32_t value;
    size_t count;
    uint32_t minimum;
    if (first >= 0xc2u && first <= 0xdfu) {
        value = (uint32_t)(first & 0x1fu);
        count = 2u;
        minimum = 0x80u;
    } else if (first >= 0xe0u && first <= 0xefu) {
        value = (uint32_t)(first & 0x0fu);
        count = 3u;
        minimum = 0x800u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        value = (uint32_t)(first & 0x07u);
        count = 4u;
        minimum = 0x10000u;
    } else {
        return false;
    }
    if (count > len - pos)
        return false;
    for (size_t index = 1u; index < count; index++) {
        uint8_t continuation = bytes[pos + index];
        if ((continuation & 0xc0u) != 0x80u)
            return false;
        value = (value << 6u) | (uint32_t)(continuation & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu))
        return false;
    *codepoint = value;
    *width = count;
    return true;
}

static bool utf8_validate(const uint8_t *bytes,
                          size_t len,
                          const char *source,
                          char *error,
                          size_t error_cap) {
    size_t pos = 0u;
    while (pos < len) {
        uint32_t codepoint;
        size_t width;
        if (!utf8_decode(bytes, len, pos, &codepoint, &width))
            return set_error(error,
                             error_cap,
                             "%s: invalid UTF-8 at byte offset %zu",
                             source != NULL ? source : "<text>",
                             pos);
        (void)codepoint;
        pos += width;
    }
    return true;
}

static bool unicode_space(uint32_t codepoint) {
    if ((codepoint >= 0x09u && codepoint <= 0x0du) ||
        (codepoint >= 0x1cu && codepoint <= 0x20u))
        return true;
    if (codepoint == 0x85u || codepoint == 0xa0u || codepoint == 0x1680u ||
        codepoint == 0x2028u || codepoint == 0x2029u ||
        codepoint == 0x202fu || codepoint == 0x205fu ||
        codepoint == 0x3000u)
        return true;
    return codepoint >= 0x2000u && codepoint <= 0x200au;
}

static bool append_codepoint(ByteBuffer *buffer, uint32_t codepoint) {
    uint8_t encoded[4];
    size_t len;
    if (codepoint <= 0x7fu) {
        encoded[0] = (uint8_t)codepoint;
        len = 1u;
    } else if (codepoint <= 0x7ffu) {
        encoded[0] = (uint8_t)(0xc0u | (codepoint >> 6u));
        encoded[1] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        len = 2u;
    } else if (codepoint <= 0xffffu &&
               !(codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        encoded[0] = (uint8_t)(0xe0u | (codepoint >> 12u));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        len = 3u;
    } else if (codepoint <= 0x10ffffu) {
        encoded[0] = (uint8_t)(0xf0u | (codepoint >> 18u));
        encoded[1] = (uint8_t)(0x80u | ((codepoint >> 12u) & 0x3fu));
        encoded[2] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[3] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        len = 4u;
    } else {
        return false;
    }
    return buffer_append(buffer, encoded, len);
}

static Node *node_new(NodeKind kind) {
    Node *node = (Node *)calloc(1u, sizeof(*node));
    if (node != NULL)
        node->kind = kind;
    return node;
}

static void node_free(Node *node) {
    if (node == NULL)
        return;
    for (size_t index = 0u; index < node->item_count; index++)
        node_free(node->items[index]);
    free(node->items);
    free(node->text);
    free(node);
}

static bool node_set_text(Node *node, const uint8_t *bytes, size_t len) {
    size_t allocation;
    if (!checked_add(len, 1u, &allocation))
        return false;
    node->text = (uint8_t *)malloc(allocation);
    if (node->text == NULL)
        return false;
    if (len > 0u)
        memcpy(node->text, bytes, len);
    node->text[len] = 0u;
    node->text_len = len;
    return true;
}

static bool node_add(Node *node, Node *item) {
    if (node->item_count == node->item_cap) {
        size_t next = node->item_cap == 0u ? 8u : node->item_cap * 2u;
        if (next < node->item_cap || next > SIZE_MAX / sizeof(*node->items))
            return false;
        Node **grown = (Node **)realloc(node->items,
                                       next * sizeof(*node->items));
        if (grown == NULL)
            return false;
        node->items = grown;
        node->item_cap = next;
    }
    node->items[node->item_count++] = item;
    return true;
}

static bool parser_skip(Parser *parser) {
    while (parser->pos < parser->len) {
        if (parser->bytes[parser->pos] == (uint8_t)';') {
            while (parser->pos < parser->len &&
                   parser->bytes[parser->pos] != (uint8_t)'\n')
                parser->pos++;
            continue;
        }
        uint32_t codepoint;
        size_t width;
        if (!utf8_decode(parser->bytes,
                         parser->len,
                         parser->pos,
                         &codepoint,
                         &width))
            return parser_error(parser,
                                "invalid UTF-8 at byte offset %zu",
                                parser->pos);
        if (!unicode_space(codepoint))
            break;
        parser->pos += width;
    }
    return true;
}

static int hex_digit(uint8_t byte) {
    if (byte >= (uint8_t)'0' && byte <= (uint8_t)'9')
        return (int)(byte - (uint8_t)'0');
    if (byte >= (uint8_t)'a' && byte <= (uint8_t)'f')
        return 10 + (int)(byte - (uint8_t)'a');
    if (byte >= (uint8_t)'A' && byte <= (uint8_t)'F')
        return 10 + (int)(byte - (uint8_t)'A');
    return -1;
}

static bool parse_hex4(Parser *parser, uint32_t *value) {
    if (parser->len - parser->pos < 4u)
        return parser_error(parser,
                            "incomplete Unicode escape at byte offset %zu",
                            parser->pos);
    uint32_t parsed = 0u;
    for (size_t index = 0u; index < 4u; index++) {
        int digit = hex_digit(parser->bytes[parser->pos + index]);
        if (digit < 0)
            return parser_error(parser,
                                "invalid Unicode escape at byte offset %zu",
                                parser->pos + index);
        parsed = (parsed << 4u) | (uint32_t)digit;
    }
    parser->pos += 4u;
    *value = parsed;
    return true;
}

static Node *parse_string(Parser *parser) {
    size_t start = parser->pos;
    parser->pos++;
    ByteBuffer decoded = {0};
    while (parser->pos < parser->len) {
        uint8_t byte = parser->bytes[parser->pos];
        if (byte == (uint8_t)'"') {
            parser->pos++;
            Node *node = node_new(NODE_STRING);
            if (node == NULL ||
                !node_set_text(node, decoded.bytes, decoded.len)) {
                node_free(node);
                buffer_destroy(&decoded);
                (void)parser_error(parser, "out of memory while parsing string");
                return NULL;
            }
            buffer_destroy(&decoded);
            return node;
        }
        if (byte == (uint8_t)'\\') {
            parser->pos++;
            if (parser->pos >= parser->len) {
                (void)parser_error(parser,
                                   "unterminated string at byte offset %zu",
                                   start);
                buffer_destroy(&decoded);
                return NULL;
            }
            uint8_t escape = parser->bytes[parser->pos++];
            uint32_t codepoint;
            switch (escape) {
            case (uint8_t)'"': codepoint = '"'; break;
            case (uint8_t)'\\': codepoint = '\\'; break;
            case (uint8_t)'/': codepoint = '/'; break;
            case (uint8_t)'b': codepoint = 0x08u; break;
            case (uint8_t)'f': codepoint = 0x0cu; break;
            case (uint8_t)'n': codepoint = 0x0au; break;
            case (uint8_t)'r': codepoint = 0x0du; break;
            case (uint8_t)'t': codepoint = 0x09u; break;
            case (uint8_t)'u': {
                if (!parse_hex4(parser, &codepoint)) {
                    buffer_destroy(&decoded);
                    return NULL;
                }
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                    if (parser->len - parser->pos < 6u ||
                        parser->bytes[parser->pos] != (uint8_t)'\\' ||
                        parser->bytes[parser->pos + 1u] != (uint8_t)'u') {
                        (void)parser_error(
                            parser,
                            "unpaired high surrogate in string at byte offset %zu",
                            parser->pos - 4u);
                        buffer_destroy(&decoded);
                        return NULL;
                    }
                    parser->pos += 2u;
                    uint32_t low;
                    if (!parse_hex4(parser, &low)) {
                        buffer_destroy(&decoded);
                        return NULL;
                    }
                    if (low < 0xdc00u || low > 0xdfffu) {
                        (void)parser_error(
                            parser,
                            "invalid low surrogate in string at byte offset %zu",
                            parser->pos - 4u);
                        buffer_destroy(&decoded);
                        return NULL;
                    }
                    codepoint = 0x10000u +
                                ((codepoint - 0xd800u) << 10u) +
                                (low - 0xdc00u);
                } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                    (void)parser_error(
                        parser,
                        "unpaired low surrogate in string at byte offset %zu",
                        parser->pos - 4u);
                    buffer_destroy(&decoded);
                    return NULL;
                }
                break;
            }
            default:
                (void)parser_error(parser,
                                   "invalid string escape at byte offset %zu",
                                   parser->pos - 1u);
                buffer_destroy(&decoded);
                return NULL;
            }
            if (!append_codepoint(&decoded, codepoint)) {
                (void)parser_error(parser, "out of memory while parsing string");
                buffer_destroy(&decoded);
                return NULL;
            }
            continue;
        }
        uint32_t codepoint;
        size_t width;
        if (!utf8_decode(parser->bytes,
                         parser->len,
                         parser->pos,
                         &codepoint,
                         &width)) {
            (void)parser_error(parser,
                               "invalid UTF-8 in string at byte offset %zu",
                               parser->pos);
            buffer_destroy(&decoded);
            return NULL;
        }
        if (codepoint < 0x20u) {
            (void)parser_error(parser,
                               "unescaped control character in string at byte offset %zu",
                               parser->pos);
            buffer_destroy(&decoded);
            return NULL;
        }
        if (!buffer_append(&decoded, parser->bytes + parser->pos, width)) {
            (void)parser_error(parser, "out of memory while parsing string");
            buffer_destroy(&decoded);
            return NULL;
        }
        parser->pos += width;
    }
    (void)parser_error(parser,
                       "unterminated string at byte offset %zu",
                       start);
    buffer_destroy(&decoded);
    return NULL;
}

static bool atom_is_integer(const uint8_t *bytes, size_t len) {
    size_t pos = 0u;
    if (len > 0u && bytes[0] == (uint8_t)'-')
        pos = 1u;
    if (pos == len)
        return false;
    for (; pos < len; pos++) {
        if (bytes[pos] < (uint8_t)'0' || bytes[pos] > (uint8_t)'9')
            return false;
    }
    return true;
}

static bool node_set_canonical_integer(Node *node,
                                       const uint8_t *bytes,
                                       size_t len) {
    bool negative = bytes[0] == (uint8_t)'-';
    size_t pos = negative ? 1u : 0u;
    while (pos < len && bytes[pos] == (uint8_t)'0')
        pos++;
    if (pos == len) {
        static const uint8_t zero = (uint8_t)'0';
        return node_set_text(node, &zero, 1u);
    }
    size_t digits = len - pos;
    size_t output_len = digits + (negative ? 1u : 0u);
    uint8_t *normalized = (uint8_t *)malloc(output_len);
    if (normalized == NULL)
        return false;
    size_t output_pos = 0u;
    if (negative)
        normalized[output_pos++] = (uint8_t)'-';
    memcpy(normalized + output_pos, bytes + pos, digits);
    bool ok = node_set_text(node, normalized, output_len);
    free(normalized);
    return ok;
}

static bool delimiter_at(Parser *parser, size_t pos, size_t *width) {
    uint8_t byte = parser->bytes[pos];
    if (byte == (uint8_t)'(' || byte == (uint8_t)')' ||
        byte == (uint8_t)';' || byte == (uint8_t)'"') {
        *width = 1u;
        return true;
    }
    uint32_t codepoint;
    if (!utf8_decode(parser->bytes, parser->len, pos, &codepoint, width))
        return true;
    return unicode_space(codepoint);
}

static Node *parse_atom(Parser *parser) {
    size_t start = parser->pos;
    while (parser->pos < parser->len) {
        size_t width;
        if (delimiter_at(parser, parser->pos, &width))
            break;
        parser->pos += width;
    }
    size_t len = parser->pos - start;
    if (len == 0u) {
        (void)parser_error(parser,
                           "invalid token at byte offset %zu",
                           start);
        return NULL;
    }
    NodeKind kind = NODE_SYMBOL;
    const uint8_t *text = parser->bytes + start;
    size_t text_len = len;
    if (text[0] == (uint8_t)'?' && len > 1u) {
        kind = NODE_VARIABLE;
        text++;
        text_len--;
    } else if (atom_is_integer(text, text_len)) {
        kind = NODE_INTEGER;
    }
    Node *node = node_new(kind);
    if (node == NULL) {
        (void)parser_error(parser, "out of memory while parsing atom");
        return NULL;
    }
    bool ok = kind == NODE_INTEGER
                  ? node_set_canonical_integer(node, text, text_len)
                  : node_set_text(node, text, text_len);
    if (!ok) {
        node_free(node);
        (void)parser_error(parser, "out of memory while parsing atom");
        return NULL;
    }
    return node;
}

static Node *parse_node(Parser *parser, size_t depth);

static Node *parse_list(Parser *parser, size_t depth) {
    parser->pos++;
    Node *list = node_new(NODE_LIST);
    if (list == NULL) {
        (void)parser_error(parser, "out of memory while parsing list");
        return NULL;
    }
    for (;;) {
        if (!parser_skip(parser)) {
            node_free(list);
            return NULL;
        }
        if (parser->pos >= parser->len) {
            (void)parser_error(parser, "unclosed '('");
            node_free(list);
            return NULL;
        }
        if (parser->bytes[parser->pos] == (uint8_t)')') {
            parser->pos++;
            return list;
        }
        Node *item = parse_node(parser, depth + 1u);
        if (item == NULL) {
            node_free(list);
            return NULL;
        }
        if (!node_add(list, item)) {
            node_free(item);
            node_free(list);
            (void)parser_error(parser, "out of memory while parsing list");
            return NULL;
        }
    }
}

static Node *parse_node(Parser *parser, size_t depth) {
    if (depth > FHGSLT_MAX_DEPTH) {
        (void)parser_error(parser,
                           "S-expression nesting exceeds %u",
                           FHGSLT_MAX_DEPTH);
        return NULL;
    }
    if (!parser_skip(parser))
        return NULL;
    if (parser->pos >= parser->len) {
        (void)parser_error(parser, "unexpected end of input");
        return NULL;
    }
    uint8_t byte = parser->bytes[parser->pos];
    if (byte == (uint8_t)'(')
        return parse_list(parser, depth);
    if (byte == (uint8_t)')') {
        (void)parser_error(parser, "unexpected ')' at byte offset %zu", parser->pos);
        return NULL;
    }
    if (byte == (uint8_t)'"')
        return parse_string(parser);
    return parse_atom(parser);
}

static bool parse_single_root(const FHGSLTInput *input,
                              Node **out,
                              char *error,
                              size_t error_cap) {
    if (input->bytes == NULL && input->len != 0u)
        return set_error(error, error_cap, "%s: null input", input->source);
    if (!utf8_validate(input->bytes,
                       input->len,
                       input->source,
                       error,
                       error_cap))
        return false;
    Parser parser = {
        .bytes = input->bytes,
        .len = input->len,
        .source = input->source,
        .error = error,
        .error_cap = error_cap,
    };
    Node *root = parse_node(&parser, 0u);
    if (root == NULL)
        return false;
    if (!parser_skip(&parser)) {
        node_free(root);
        return false;
    }
    if (parser.pos != parser.len) {
        node_free(root);
        return parser_error(&parser,
                            "expected one gslt-presentation-v1 form");
    }
    *out = root;
    return true;
}

static Text node_text(const Node *node) {
    Text text = {.bytes = node->text, .len = node->text_len};
    return text;
}

static bool text_equal(Text left, Text right) {
    return left.len == right.len &&
           (left.len == 0u || memcmp(left.bytes, right.bytes, left.len) == 0);
}

static bool text_literal(Text text, const char *literal) {
    size_t len = strlen(literal);
    return text.len == len &&
           (len == 0u || memcmp(text.bytes, literal, len) == 0);
}

static int text_compare(Text left, Text right) {
    size_t common = left.len < right.len ? left.len : right.len;
    int order = common == 0u ? 0 : memcmp(left.bytes, right.bytes, common);
    if (order != 0)
        return order;
    if (left.len < right.len)
        return -1;
    if (left.len > right.len)
        return 1;
    return 0;
}

static bool node_is_symbol(const Node *node, const char *literal) {
    return node != NULL && node->kind == NODE_SYMBOL &&
           text_literal(node_text(node), literal);
}

static bool require_symbol(const Node *node,
                           Text *out,
                           const char *source,
                           const char *context,
                           char *error,
                           size_t error_cap) {
    if (node == NULL || node->kind != NODE_SYMBOL || node->text_len == 0u)
        return set_error(error,
                         error_cap,
                         "%s: %s must be a nonempty symbol",
                         source,
                         context);
    *out = node_text(node);
    return true;
}

static bool positive_size(const Node *node, size_t *out) {
    if (node == NULL || node->kind != NODE_INTEGER || node->text_len == 0u ||
        node->text[0] == (uint8_t)'-' ||
        (node->text_len == 1u && node->text[0] == (uint8_t)'0'))
        return false;
    size_t value = 0u;
    for (size_t index = 0u; index < node->text_len; index++) {
        unsigned digit = (unsigned)(node->text[index] - (uint8_t)'0');
        if (value > (SIZE_MAX - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

static char *copy_c_string(const char *source) {
    const char *actual = source != NULL ? source : "<text>";
    size_t len = strlen(actual);
    char *copy = (char *)malloc(len + 1u);
    if (copy != NULL)
        memcpy(copy, actual, len + 1u);
    return copy;
}

static void presentation_destroy(Presentation *presentation) {
    if (presentation == NULL)
        return;
    free(presentation->operators);
    free(presentation->rules);
    node_free(presentation->root);
    free(presentation->source);
    memset(presentation, 0, sizeof(*presentation));
}

static bool presentation_add_operator(Presentation *presentation,
                                      OperatorDecl declaration,
                                      char *error,
                                      size_t error_cap) {
    if (presentation->operator_count == SIZE_MAX / sizeof(*presentation->operators))
        return set_error(error, error_cap, "operator table is too large");
    size_t next = presentation->operator_count + 1u;
    OperatorDecl *grown = (OperatorDecl *)realloc(
        presentation->operators, next * sizeof(*presentation->operators));
    if (grown == NULL)
        return set_error(error, error_cap, "out of memory building operator table");
    presentation->operators = grown;
    presentation->operators[presentation->operator_count++] = declaration;
    return true;
}

static bool presentation_add_rule(Presentation *presentation,
                                  RuleDecl declaration,
                                  char *error,
                                  size_t error_cap) {
    if (presentation->rule_count == SIZE_MAX / sizeof(*presentation->rules))
        return set_error(error, error_cap, "rule table is too large");
    size_t next = presentation->rule_count + 1u;
    RuleDecl *grown = (RuleDecl *)realloc(
        presentation->rules, next * sizeof(*presentation->rules));
    if (grown == NULL)
        return set_error(error, error_cap, "out of memory building rule table");
    presentation->rules = grown;
    presentation->rules[presentation->rule_count++] = declaration;
    return true;
}

static bool parse_presentation(const FHGSLTInput *input,
                               Presentation *presentation,
                               char *error,
                               size_t error_cap) {
    memset(presentation, 0, sizeof(*presentation));
    if (!parse_single_root(input, &presentation->root, error, error_cap))
        return false;
    presentation->source = copy_c_string(input->source);
    if (presentation->source == NULL) {
        presentation_destroy(presentation);
        return set_error(error, error_cap, "out of memory copying source name");
    }
    Node *root = presentation->root;
    if (root->kind != NODE_LIST || root->item_count < 2u ||
        !node_is_symbol(root->items[0], "gslt-presentation-v1")) {
        presentation_destroy(presentation);
        return set_error(error,
                         error_cap,
                         "%s: expected gslt-presentation-v1 root",
                         input->source);
    }
    if (!require_symbol(root->items[1],
                        &presentation->name,
                        input->source,
                        "presentation name",
                        error,
                        error_cap)) {
        presentation_destroy(presentation);
        return false;
    }

    Node *signature = NULL;
    Node *equations = NULL;
    Node *rewrites = NULL;
    for (size_t index = 2u; index < root->item_count; index++) {
        Node *field = root->items[index];
        if (field->kind != NODE_LIST || field->item_count == 0u ||
            field->items[0]->kind != NODE_SYMBOL) {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: malformed presentation field",
                             input->source);
        }
        Node **slot = NULL;
        if (node_is_symbol(field->items[0], "signature"))
            slot = &signature;
        else if (node_is_symbol(field->items[0], "equations"))
            slot = &equations;
        else if (node_is_symbol(field->items[0], "rewrites"))
            slot = &rewrites;
        else {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: unknown presentation field",
                             input->source);
        }
        if (*slot != NULL) {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: duplicate presentation field",
                             input->source);
        }
        *slot = field;
    }
    if (signature == NULL || equations == NULL || rewrites == NULL) {
        presentation_destroy(presentation);
        return set_error(error,
                         error_cap,
                         "%s: missing presentation field",
                         input->source);
    }
    for (size_t index = 1u; index < signature->item_count; index++) {
        Node *raw = signature->items[index];
        Text name;
        size_t arity;
        if (raw->kind != NODE_LIST || raw->item_count != 3u ||
            !node_is_symbol(raw->items[0], "operator") ||
            !require_symbol(raw->items[1],
                            &name,
                            input->source,
                            "operator name",
                            error,
                            error_cap) ||
            !positive_size(raw->items[2], &arity)) {
            presentation_destroy(presentation);
            return set_error(
                error,
                error_cap,
                "%s: operator declaration must be (operator NAME POSITIVE-ARITY)",
                input->source);
        }
        for (size_t prior = 0u; prior < presentation->operator_count; prior++) {
            OperatorDecl old = presentation->operators[prior];
            if (old.arity == arity && text_equal(old.name, name)) {
                presentation_destroy(presentation);
                return set_error(error,
                                 error_cap,
                                 "%s: duplicate operator declaration",
                                 input->source);
            }
        }
        OperatorDecl declaration = {.name = name, .arity = arity};
        if (!presentation_add_operator(presentation,
                                       declaration,
                                       error,
                                       error_cap)) {
            presentation_destroy(presentation);
            return false;
        }
    }

    if (equations->item_count != 1u) {
        presentation_destroy(presentation);
        return set_error(
            error,
            error_cap,
            "%s: v1 admits identity equations only; authored equations are unsupported",
            input->source);
    }

    for (size_t index = 1u; index < rewrites->item_count; index++) {
        Node *raw = rewrites->items[index];
        Text name;
        if (raw->kind != NODE_LIST || raw->item_count != 4u ||
            !node_is_symbol(raw->items[0], "rule") ||
            !require_symbol(raw->items[1],
                            &name,
                            input->source,
                            "rule name",
                            error,
                            error_cap)) {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: malformed rule",
                             input->source);
        }
        Node *head = raw->items[2];
        Node *body = raw->items[3];
        if (head->kind != NODE_LIST || head->item_count != 2u ||
            !node_is_symbol(head->items[0], "head")) {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: malformed rule head",
                             input->source);
        }
        if (body->kind != NODE_LIST || body->item_count == 0u ||
            !node_is_symbol(body->items[0], "body")) {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: malformed rule body",
                             input->source);
        }
        RuleDecl declaration = {
            .name = name,
            .head = head->items[1],
            .body = body->items + 1u,
            .body_count = body->item_count - 1u,
        };
        if (!presentation_add_rule(presentation,
                                   declaration,
                                   error,
                                   error_cap)) {
            presentation_destroy(presentation);
            return false;
        }
    }
    if (presentation->rule_count > 1u) {
        RuleDecl **sorted_rules;
        if (presentation->rule_count > SIZE_MAX / sizeof(*sorted_rules)) {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: rule inventory is too large",
                             input->source);
        }
        sorted_rules = malloc(
            presentation->rule_count * sizeof(*sorted_rules));
        if (sorted_rules == NULL) {
            presentation_destroy(presentation);
            return set_error(error,
                             error_cap,
                             "%s: out of memory validating rule names",
                             input->source);
        }
        for (size_t index = 0u;
             index < presentation->rule_count; index++)
            sorted_rules[index] = &presentation->rules[index];
        qsort(sorted_rules,
              presentation->rule_count,
              sizeof(*sorted_rules),
              compare_rule_pointer);
        for (size_t index = 1u;
             index < presentation->rule_count; index++) {
            if (text_equal(sorted_rules[index - 1u]->name,
                           sorted_rules[index]->name)) {
                free(sorted_rules);
                presentation_destroy(presentation);
                return set_error(error,
                                 error_cap,
                                 "%s: duplicate rule name",
                                 input->source);
            }
        }
        free(sorted_rules);
    }
    return true;
}

static bool operator_exists(const FHGSLTPackage *package,
                            Text name,
                            size_t arity) {
    for (size_t p = 0u; p < package->presentation_count; p++) {
        const Presentation *presentation = &package->presentations[p];
        for (size_t o = 0u; o < presentation->operator_count; o++) {
            OperatorDecl declaration = presentation->operators[o];
            if (declaration.arity == arity && text_equal(declaration.name, name))
                return true;
        }
    }
    return false;
}

static bool validate_term(const FHGSLTPackage *package,
                          const Presentation *presentation,
                          const RuleDecl *rule,
                          const Node *term,
                          size_t depth,
                          char *error,
                          size_t error_cap) {
    if (depth > FHGSLT_MAX_DEPTH)
        return set_error(error,
                         error_cap,
                         "%s: term nesting exceeds %u",
                         presentation->source,
                         FHGSLT_MAX_DEPTH);
    if (term->kind == NODE_VARIABLE) {
        if (term->text_len == 0u ||
            (term->text_len == 1u && term->text[0] == (uint8_t)'_'))
            return set_error(error,
                             error_cap,
                             "%s: anonymous variable in rule %.*s",
                             presentation->source,
                             (int)rule->name.len,
                             (const char *)rule->name.bytes);
        return true;
    }
    if (term->kind != NODE_LIST)
        return true;
    if (term->item_count == 0u || term->items[0]->kind != NODE_SYMBOL)
        return set_error(error,
                         error_cap,
                         "%s: malformed application in rule %.*s",
                         presentation->source,
                         (int)rule->name.len,
                         (const char *)rule->name.bytes);
    Text head = node_text(term->items[0]);
    size_t arity = term->item_count - 1u;
    if (!operator_exists(package, head, arity))
        return set_error(error,
                         error_cap,
                         "%s: undeclared operator %.*s/%zu in rule %.*s",
                         presentation->source,
                         (int)head.len,
                         (const char *)head.bytes,
                         arity,
                         (int)rule->name.len,
                         (const char *)rule->name.bytes);
    for (size_t index = 1u; index < term->item_count; index++) {
        if (!validate_term(package,
                           presentation,
                           rule,
                           term->items[index],
                           depth + 1u,
                           error,
                           error_cap))
            return false;
    }
    return true;
}

static bool validate_package(FHGSLTPackage *package,
                             char *error,
                             size_t error_cap) {
    Presentation **sorted_presentations = NULL;
    RuleDecl **sorted_rules = NULL;
    size_t total_rules = 0u;
    bool ok = false;

    if (package->presentation_count > 1u) {
        if (package->presentation_count >
            SIZE_MAX / sizeof(*sorted_presentations)) {
            set_error(error, error_cap,
                      "presentation inventory is too large");
            goto done;
        }
        sorted_presentations = malloc(
            package->presentation_count * sizeof(*sorted_presentations));
        if (sorted_presentations == NULL) {
            set_error(error, error_cap,
                      "out of memory validating presentation names");
            goto done;
        }
        for (size_t index = 0u;
             index < package->presentation_count; index++)
            sorted_presentations[index] = &package->presentations[index];
        qsort(sorted_presentations,
              package->presentation_count,
              sizeof(*sorted_presentations),
              compare_presentation_pointer);
        for (size_t index = 1u;
             index < package->presentation_count; index++) {
            if (text_equal(sorted_presentations[index - 1u]->name,
                           sorted_presentations[index]->name)) {
                set_error(error, error_cap,
                          "duplicate presentation name");
                goto done;
            }
        }
    }

    for (size_t p = 0u; p < package->presentation_count; p++) {
        if (package->presentations[p].rule_count >
            SIZE_MAX - total_rules) {
            set_error(error, error_cap,
                      "composed rule inventory is too large");
            goto done;
        }
        total_rules += package->presentations[p].rule_count;
    }
    if (total_rules > 1u) {
        if (total_rules > SIZE_MAX / sizeof(*sorted_rules)) {
            set_error(error, error_cap,
                      "composed rule inventory is too large");
            goto done;
        }
        sorted_rules = malloc(total_rules * sizeof(*sorted_rules));
        if (sorted_rules == NULL) {
            set_error(error, error_cap,
                      "out of memory validating composed rule names");
            goto done;
        }
        size_t write = 0u;
        for (size_t p = 0u; p < package->presentation_count; p++) {
            Presentation *presentation = &package->presentations[p];
            for (size_t r = 0u; r < presentation->rule_count; r++)
                sorted_rules[write++] = &presentation->rules[r];
        }
        qsort(sorted_rules,
              total_rules,
              sizeof(*sorted_rules),
              compare_rule_pointer);
        for (size_t index = 1u; index < total_rules; index++) {
            if (text_equal(sorted_rules[index - 1u]->name,
                           sorted_rules[index]->name)) {
                set_error(error, error_cap,
                          "duplicate composed rule name");
                goto done;
            }
        }
    }

    for (size_t p = 0u; p < package->presentation_count; p++) {
        Presentation *presentation = &package->presentations[p];
        for (size_t r = 0u; r < presentation->rule_count; r++) {
            RuleDecl *rule = &presentation->rules[r];
            if (rule->head->kind != NODE_LIST) {
                set_error(error,
                          error_cap,
                          "%s: rule head must be an application",
                          presentation->source);
                goto done;
            }
            if (!validate_term(package,
                               presentation,
                               rule,
                               rule->head,
                               0u,
                               error,
                               error_cap))
                goto done;
            for (size_t b = 0u; b < rule->body_count; b++) {
                if (rule->body[b]->kind != NODE_LIST) {
                    set_error(error,
                              error_cap,
                              "%s: rule body must contain applications",
                              presentation->source);
                    goto done;
                }
                if (!validate_term(package,
                                   presentation,
                                   rule,
                                   rule->body[b],
                                   0u,
                                   error,
                                   error_cap))
                    goto done;
            }
        }
    }
    ok = true;

done:
    free(sorted_presentations);
    free(sorted_rules);
    return ok;
}

bool fhgslt_package_from_inputs(const FHGSLTInput *inputs,
                                size_t input_count,
                                FHGSLTPackage **out,
                                char *error,
                                size_t error_cap) {
    if (out == NULL)
        return set_error(error, error_cap, "output package pointer is null");
    *out = NULL;
    if (inputs == NULL || input_count == 0u)
        return set_error(error, error_cap, "at least one presentation is required");
    if (input_count > SIZE_MAX / sizeof(Presentation))
        return set_error(error, error_cap, "presentation table is too large");
    FHGSLTPackage *package = (FHGSLTPackage *)calloc(1u, sizeof(*package));
    if (package == NULL)
        return set_error(error, error_cap, "out of memory allocating package");
    package->presentations =
        (Presentation *)calloc(input_count, sizeof(*package->presentations));
    if (package->presentations == NULL) {
        free(package);
        return set_error(error, error_cap, "out of memory allocating presentations");
    }
    package->presentation_count = input_count;
    for (size_t index = 0u; index < input_count; index++) {
        if (!parse_presentation(&inputs[index],
                                &package->presentations[index],
                                error,
                                error_cap)) {
            fhgslt_package_free(package);
            return false;
        }
    }
    if (!validate_package(package, error, error_cap)) {
        fhgslt_package_free(package);
        return false;
    }
    *out = package;
    return true;
}

static bool read_path(const char *path,
                      uint8_t **out,
                      size_t *out_len,
                      char *error,
                      size_t error_cap) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return set_error(error, error_cap, "%s: cannot open file", path);
    ByteBuffer buffer = {0};
    uint8_t chunk[16384];
    while (!feof(file)) {
        size_t count = fread(chunk, 1u, sizeof(chunk), file);
        if (count > 0u && !buffer_append(&buffer, chunk, count)) {
            fclose(file);
            buffer_destroy(&buffer);
            return set_error(error, error_cap, "%s: out of memory reading file", path);
        }
        if (ferror(file)) {
            fclose(file);
            buffer_destroy(&buffer);
            return set_error(error, error_cap, "%s: error reading file", path);
        }
    }
    if (fclose(file) != 0) {
        buffer_destroy(&buffer);
        return set_error(error, error_cap, "%s: error closing file", path);
    }
    *out = buffer.bytes;
    *out_len = buffer.len;
    return true;
}

bool fhgslt_package_from_paths(const char *const *paths,
                               size_t path_count,
                               FHGSLTPackage **out,
                               char *error,
                               size_t error_cap) {
    if (paths == NULL || path_count == 0u)
        return set_error(error, error_cap, "at least one presentation is required");
    if (path_count > SIZE_MAX / sizeof(FHGSLTInput) ||
        path_count > SIZE_MAX / sizeof(uint8_t *))
        return set_error(error, error_cap, "path table is too large");
    FHGSLTInput *inputs =
        (FHGSLTInput *)calloc(path_count, sizeof(*inputs));
    uint8_t **buffers = (uint8_t **)calloc(path_count, sizeof(*buffers));
    if (inputs == NULL || buffers == NULL) {
        free(inputs);
        free(buffers);
        return set_error(error, error_cap, "out of memory allocating path inputs");
    }
    bool ok = true;
    for (size_t index = 0u; index < path_count; index++) {
        if (paths[index] == NULL ||
            !read_path(paths[index],
                       &buffers[index],
                       &inputs[index].len,
                       error,
                       error_cap)) {
            ok = false;
            break;
        }
        inputs[index].bytes = buffers[index];
        inputs[index].source = paths[index];
    }
    if (ok)
        ok = fhgslt_package_from_inputs(inputs,
                                        path_count,
                                        out,
                                        error,
                                        error_cap);
    for (size_t index = 0u; index < path_count; index++)
        free(buffers[index]);
    free(buffers);
    free(inputs);
    return ok;
}

void fhgslt_package_free(FHGSLTPackage *package) {
    if (package == NULL)
        return;
    for (size_t index = 0u; index < package->presentation_count; index++)
        presentation_destroy(&package->presentations[index]);
    free(package->presentations);
    free(package);
}

size_t fhgslt_package_presentation_count(const FHGSLTPackage *package) {
    return package != NULL ? package->presentation_count : 0u;
}

size_t fhgslt_package_operator_count(const FHGSLTPackage *package) {
    size_t count = 0u;
    if (package == NULL)
        return 0u;
    for (size_t index = 0u; index < package->presentation_count; index++)
        count += package->presentations[index].operator_count;
    return count;
}

size_t fhgslt_package_rule_count(const FHGSLTPackage *package) {
    size_t count = 0u;
    if (package == NULL)
        return 0u;
    for (size_t index = 0u; index < package->presentation_count; index++)
        count += package->presentations[index].rule_count;
    return count;
}

typedef struct {
    Text name;
    size_t head_occurrence_count;
    size_t body_occurrence_count;
    size_t body_goal_count;
    size_t last_body_goal;
} VariableUseV1;

typedef struct {
    VariableUseV1 *items;
    size_t len;
    size_t cap;
} VariableUseTableV1;

static bool shape_increment_v1(size_t *value,
                               char *error,
                               size_t error_cap) {
    if (*value == SIZE_MAX)
        return set_error(error, error_cap,
                         "finite-Horn structural count overflow");
    (*value)++;
    return true;
}

static VariableUseV1 *variable_use_v1(
    VariableUseTableV1 *table,
    Text name,
    char *error,
    size_t error_cap) {
    for (size_t index = 0u; index < table->len; index++) {
        if (text_equal(table->items[index].name, name))
            return &table->items[index];
    }
    if (table->len == table->cap) {
        size_t next = table->cap == 0u ? 8u : table->cap * 2u;
        if (next < table->cap ||
            next > SIZE_MAX / sizeof(*table->items)) {
            set_error(error, error_cap,
                      "finite-Horn variable-use table is too large");
            return NULL;
        }
        VariableUseV1 *grown = (VariableUseV1 *)realloc(
            table->items, next * sizeof(*table->items));
        if (grown == NULL) {
            set_error(error, error_cap,
                      "out of memory inspecting finite-Horn variables");
            return NULL;
        }
        table->items = grown;
        table->cap = next;
    }
    VariableUseV1 *use = &table->items[table->len++];
    memset(use, 0, sizeof(*use));
    use->name = name;
    return use;
}

static bool collect_variable_uses_v1(
    const Node *node,
    bool in_head,
    size_t body_goal,
    size_t depth,
    VariableUseTableV1 *uses,
    char *error,
    size_t error_cap) {
    if (depth > FHGSLT_MAX_DEPTH)
        return set_error(error, error_cap,
                         "term nesting exceeds %u during shape inspection",
                         FHGSLT_MAX_DEPTH);
    if (node->kind == NODE_VARIABLE) {
        VariableUseV1 *use = variable_use_v1(
            uses, node_text(node), error, error_cap);
        if (use == NULL)
            return false;
        if (in_head) {
            return shape_increment_v1(
                &use->head_occurrence_count, error, error_cap);
        }
        if (!shape_increment_v1(
                &use->body_occurrence_count, error, error_cap))
            return false;
        if (use->last_body_goal != body_goal) {
            use->last_body_goal = body_goal;
            if (!shape_increment_v1(
                    &use->body_goal_count, error, error_cap))
                return false;
        }
        return true;
    }
    if (node->kind != NODE_LIST)
        return true;
    for (size_t index = 0u; index < node->item_count; index++) {
        if (!collect_variable_uses_v1(
                node->items[index], in_head, body_goal, depth + 1u,
                uses, error, error_cap))
            return false;
    }
    return true;
}

static bool rule_is_direct_recursive_v1(const RuleDecl *rule) {
    const Node *head_operator = rule->head->items[0];
    size_t head_arity = rule->head->item_count - 1u;
    for (size_t index = 0u; index < rule->body_count; index++) {
        const Node *goal = rule->body[index];
        if (goal->item_count - 1u == head_arity &&
            text_equal(node_text(goal->items[0]),
                       node_text(head_operator)))
            return true;
    }
    return false;
}

bool fhgslt_package_structural_shape_v1(
    const FHGSLTPackage *package,
    FHGSLTStructuralShapeV1 *out,
    char *error,
    size_t error_cap) {
    if (package == NULL || out == NULL)
        return set_error(error, error_cap,
                         "structural-shape input is null");
    memset(out, 0, sizeof(*out));
    FHGSLTStructuralShapeV1 shape = {0};
    for (size_t presentation_index = 0u;
         presentation_index < package->presentation_count;
         presentation_index++) {
        const Presentation *presentation =
            &package->presentations[presentation_index];
        for (size_t rule_index = 0u;
             rule_index < presentation->rule_count;
             rule_index++) {
            const RuleDecl *rule = &presentation->rules[rule_index];
            VariableUseTableV1 uses = {0};
            bool ok =
                shape_increment_v1(&shape.rule_count, error, error_cap) &&
                collect_variable_uses_v1(
                    rule->head, true, 0u, 0u, &uses,
                    error, error_cap);
            for (size_t body_index = 0u;
                 ok && body_index < rule->body_count;
                 body_index++) {
                ok = collect_variable_uses_v1(
                    rule->body[body_index], false, body_index + 1u, 0u,
                    &uses, error, error_cap);
            }
            if (!ok) {
                free(uses.items);
                return false;
            }

            if (rule->body_count == 0u) {
                ok = shape_increment_v1(
                    &shape.fact_rule_count, error, error_cap);
            } else {
                ok = shape_increment_v1(
                    &shape.implication_rule_count, error, error_cap);
            }
            if (rule->body_count > SIZE_MAX - shape.body_goal_count) {
                free(uses.items);
                return set_error(error, error_cap,
                                 "finite-Horn body-goal count overflow");
            }
            shape.body_goal_count += rule->body_count;
            if (rule->body_count > shape.maximum_body_goal_count)
                shape.maximum_body_goal_count = rule->body_count;
            if (ok && rule->body_count > 1u)
                ok = shape_increment_v1(
                    &shape.multi_body_rule_count, error, error_cap);

            bool left_linear = true;
            bool cross_goal_join = false;
            bool body_only_variable = false;
            bool head_only_variable = false;
            for (size_t index = 0u; index < uses.len; index++) {
                const VariableUseV1 *use = &uses.items[index];
                if (use->head_occurrence_count > 1u)
                    left_linear = false;
                if (use->body_goal_count > 1u)
                    cross_goal_join = true;
                if (use->head_occurrence_count == 0u &&
                    use->body_occurrence_count > 0u)
                    body_only_variable = true;
                if (use->head_occurrence_count > 0u &&
                    use->body_occurrence_count == 0u)
                    head_only_variable = true;
            }
            if (ok)
                ok = shape_increment_v1(
                    left_linear
                        ? &shape.left_linear_head_rule_count
                        : &shape.nonlinear_head_rule_count,
                    error, error_cap);
            if (ok && cross_goal_join)
                ok = shape_increment_v1(
                    &shape.cross_goal_join_rule_count, error, error_cap);
            if (ok && body_only_variable)
                ok = shape_increment_v1(
                    &shape.body_only_variable_rule_count,
                    error, error_cap);
            if (ok && head_only_variable)
                ok = shape_increment_v1(
                    &shape.head_only_variable_rule_count,
                    error, error_cap);
            if (ok && rule_is_direct_recursive_v1(rule))
                ok = shape_increment_v1(
                    &shape.direct_recursive_rule_count,
                    error, error_cap);
            free(uses.items);
            if (!ok)
                return false;
        }
    }
    *out = shape;
    return true;
}

bool fhgslt_package_declares_operator(const FHGSLTPackage *package,
                                      const char *name,
                                      size_t arity) {
    if (package == NULL || name == NULL)
        return false;
    size_t name_len = strlen(name);
    for (size_t presentation_index = 0u;
         presentation_index < package->presentation_count;
         presentation_index++) {
        const Presentation *presentation =
            &package->presentations[presentation_index];
        for (size_t operator_index = 0u;
             operator_index < presentation->operator_count;
             operator_index++) {
            const OperatorDecl *operator =
                &presentation->operators[operator_index];
            if (operator->arity == arity && operator->name.len == name_len &&
                memcmp(operator->name.bytes, name, name_len) == 0)
                return true;
        }
    }
    return false;
}

static bool render_string(ByteBuffer *buffer, Text text) {
    static const char hex[] = "0123456789abcdef";
    if (!buffer_byte(buffer, (uint8_t)'"'))
        return false;
    size_t pos = 0u;
    while (pos < text.len) {
        uint32_t codepoint;
        size_t width;
        if (!utf8_decode(text.bytes, text.len, pos, &codepoint, &width))
            return false;
        const char *escape = NULL;
        switch (codepoint) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case 0x08u: escape = "\\b"; break;
        case 0x0cu: escape = "\\f"; break;
        case 0x0au: escape = "\\n"; break;
        case 0x0du: escape = "\\r"; break;
        case 0x09u: escape = "\\t"; break;
        default: break;
        }
        if (escape != NULL) {
            if (!buffer_literal(buffer, escape))
                return false;
        } else if (codepoint < 0x20u) {
            uint8_t escaped[6] = {
                (uint8_t)'\\', (uint8_t)'u', (uint8_t)'0', (uint8_t)'0',
                (uint8_t)hex[(codepoint >> 4u) & 0x0fu],
                (uint8_t)hex[codepoint & 0x0fu],
            };
            if (!buffer_append(buffer, escaped, sizeof(escaped)))
                return false;
        } else if (!buffer_append(buffer, text.bytes + pos, width)) {
            return false;
        }
        pos += width;
    }
    return buffer_byte(buffer, (uint8_t)'"');
}

static bool render_node_with_variable_marker(ByteBuffer *buffer,
                                             const Node *node,
                                             uint8_t variable_marker) {
    switch (node->kind) {
    case NODE_SYMBOL:
    case NODE_INTEGER:
        return buffer_append(buffer, node->text, node->text_len);
    case NODE_VARIABLE:
        return buffer_byte(buffer, variable_marker) &&
               buffer_append(buffer, node->text, node->text_len);
    case NODE_STRING:
        return render_string(buffer, node_text(node));
    case NODE_LIST:
        if (!buffer_byte(buffer, (uint8_t)'('))
            return false;
        for (size_t index = 0u; index < node->item_count; index++) {
            if (index > 0u && !buffer_byte(buffer, (uint8_t)' '))
                return false;
            if (!render_node_with_variable_marker(
                    buffer, node->items[index], variable_marker))
                return false;
        }
        return buffer_byte(buffer, (uint8_t)')');
    }
    return false;
}

static bool render_node(ByteBuffer *buffer, const Node *node) {
    return render_node_with_variable_marker(buffer, node, (uint8_t)'?');
}

static int compare_operator_pointer(const void *left, const void *right) {
    const OperatorDecl *a = *(const OperatorDecl *const *)left;
    const OperatorDecl *b = *(const OperatorDecl *const *)right;
    int order = text_compare(a->name, b->name);
    if (order != 0)
        return order;
    if (a->arity < b->arity)
        return -1;
    if (a->arity > b->arity)
        return 1;
    return 0;
}

static int compare_rule_pointer(const void *left, const void *right) {
    const RuleDecl *a = *(const RuleDecl *const *)left;
    const RuleDecl *b = *(const RuleDecl *const *)right;
    return text_compare(a->name, b->name);
}

static int compare_presentation_pointer(const void *left, const void *right) {
    const Presentation *a = *(const Presentation *const *)left;
    const Presentation *b = *(const Presentation *const *)right;
    return text_compare(a->name, b->name);
}

typedef struct {
    Text *variables;
    size_t count;
    size_t cap;
} VariableQuotation;

static void variable_quotation_destroy(VariableQuotation *quotation) {
    free(quotation->variables);
    memset(quotation, 0, sizeof(*quotation));
}

static bool variable_quotation_index(VariableQuotation *quotation,
                                     Text variable,
                                     size_t *index) {
    for (size_t prior = 0u; prior < quotation->count; prior++) {
        if (text_equal(quotation->variables[prior], variable)) {
            *index = prior;
            return true;
        }
    }
    if (quotation->count == quotation->cap) {
        size_t next = quotation->cap == 0u ? 8u : quotation->cap * 2u;
        if (next < quotation->cap || next > SIZE_MAX / sizeof(Text))
            return false;
        Text *grown = (Text *)realloc(quotation->variables,
                                     next * sizeof(Text));
        if (grown == NULL)
            return false;
        quotation->variables = grown;
        quotation->cap = next;
    }
    *index = quotation->count;
    quotation->variables[quotation->count++] = variable;
    return true;
}

static bool render_quoted_index(ByteBuffer *buffer, size_t index) {
    for (size_t depth = 0u; depth < index; depth++) {
        if (!buffer_literal(buffer, "(q-succ "))
            return false;
    }
    if (!buffer_literal(buffer, "q-zero"))
        return false;
    for (size_t depth = 0u; depth < index; depth++) {
        if (!buffer_byte(buffer, (uint8_t)')'))
            return false;
    }
    return true;
}

static bool render_quoted_node(ByteBuffer *buffer,
                               const Node *node,
                               VariableQuotation *quotation,
                               size_t depth);

static bool render_quoted_nodes(ByteBuffer *buffer,
                                Node *const *nodes,
                                size_t count,
                                VariableQuotation *quotation,
                                size_t depth) {
    for (size_t index = 0u; index < count; index++) {
        if (!buffer_literal(buffer, "(q-cons ") ||
            !render_quoted_node(buffer, nodes[index], quotation, depth + 1u) ||
            !buffer_byte(buffer, (uint8_t)' '))
            return false;
    }
    if (!buffer_literal(buffer, "q-nil"))
        return false;
    for (size_t index = 0u; index < count; index++) {
        if (!buffer_byte(buffer, (uint8_t)')'))
            return false;
    }
    return true;
}

static bool render_quoted_node(ByteBuffer *buffer,
                               const Node *node,
                               VariableQuotation *quotation,
                               size_t depth) {
    if (depth > FHGSLT_MAX_DEPTH)
        return false;
    switch (node->kind) {
    case NODE_SYMBOL:
        return buffer_literal(buffer, "(q-sym ") &&
               buffer_append(buffer, node->text, node->text_len) &&
               buffer_byte(buffer, (uint8_t)')');
    case NODE_INTEGER:
        return buffer_literal(buffer, "(q-int ") &&
               buffer_append(buffer, node->text, node->text_len) &&
               buffer_byte(buffer, (uint8_t)')');
    case NODE_STRING:
        return buffer_literal(buffer, "(q-str ") &&
               render_string(buffer, node_text(node)) &&
               buffer_byte(buffer, (uint8_t)')');
    case NODE_VARIABLE: {
        size_t index;
        if (!variable_quotation_index(quotation, node_text(node), &index))
            return false;
        return buffer_literal(buffer, "(q-var ") &&
               render_quoted_index(buffer, index) &&
               buffer_byte(buffer, (uint8_t)')');
    }
    case NODE_LIST:
        if (node->item_count == 0u ||
            node->items[0]->kind != NODE_SYMBOL)
            return false;
        return buffer_literal(buffer, "(q-app (q-sym ") &&
               buffer_append(buffer,
                             node->items[0]->text,
                             node->items[0]->text_len) &&
               buffer_literal(buffer, ") ") &&
               render_quoted_nodes(buffer,
                                   node->items + 1u,
                                   node->item_count - 1u,
                                   quotation,
                                   depth + 1u) &&
               buffer_byte(buffer, (uint8_t)')');
    }
    return false;
}

static bool render_quoted_rule(ByteBuffer *buffer, const RuleDecl *rule) {
    VariableQuotation quotation = {0};
    bool ok = buffer_literal(buffer, "(q-rule (q-sym ") &&
              buffer_append(buffer, rule->name.bytes, rule->name.len) &&
              buffer_literal(buffer, ") ") &&
              render_quoted_node(buffer, rule->head, &quotation, 0u) &&
              buffer_byte(buffer, (uint8_t)' ') &&
              render_quoted_nodes(buffer,
                                  rule->body,
                                  rule->body_count,
                                  &quotation,
                                  0u) &&
              buffer_byte(buffer, (uint8_t)')');
    variable_quotation_destroy(&quotation);
    return ok;
}

bool fhgslt_package_quoted_rules(const FHGSLTPackage *package,
                                 size_t index,
                                 uint8_t **out,
                                 size_t *out_len,
                                 char *error,
                                 size_t error_cap) {
    if (package == NULL || out == NULL || out_len == NULL ||
        index >= package->presentation_count)
        return set_error(error, error_cap, "invalid quotation request");
    *out = NULL;
    *out_len = 0u;
    const Presentation *presentation = &package->presentations[index];
    RuleDecl **rules = NULL;
    if (presentation->rule_count > 0u) {
        rules = (RuleDecl **)malloc(presentation->rule_count * sizeof(*rules));
        if (rules == NULL)
            return set_error(error, error_cap, "out of memory sorting quoted rules");
        for (size_t rule = 0u; rule < presentation->rule_count; rule++)
            rules[rule] = &presentation->rules[rule];
        qsort(rules,
              presentation->rule_count,
              sizeof(*rules),
              compare_rule_pointer);
    }
    ByteBuffer buffer = {0};
    bool ok = true;
    for (size_t rule = 0u; ok && rule < presentation->rule_count; rule++) {
        ok = buffer_literal(&buffer, "(q-cons ") &&
             render_quoted_rule(&buffer, rules[rule]) &&
             buffer_byte(&buffer, (uint8_t)' ');
    }
    ok = ok && buffer_literal(&buffer, "q-nil");
    for (size_t rule = 0u; ok && rule < presentation->rule_count; rule++)
        ok = buffer_byte(&buffer, (uint8_t)')');
    ok = ok && buffer_byte(&buffer, 0u);
    free(rules);
    if (!ok) {
        buffer_destroy(&buffer);
        return set_error(error, error_cap, "out of memory quoting presentation rules");
    }
    buffer.len--;
    *out = buffer.bytes;
    *out_len = buffer.len;
    return true;
}

static bool append_text(ByteBuffer *buffer, Text text) {
    return buffer_append(buffer, text.bytes, text.len);
}

static bool canonical_presentation(const Presentation *presentation,
                                   ByteBuffer *buffer,
                                   char *error,
                                   size_t error_cap) {
    OperatorDecl **operators = NULL;
    RuleDecl **rules = NULL;
    if (presentation->operator_count > 0u) {
        operators = (OperatorDecl **)malloc(
            presentation->operator_count * sizeof(*operators));
        if (operators == NULL)
            return set_error(error,
                             error_cap,
                             "out of memory sorting operators");
        for (size_t index = 0u; index < presentation->operator_count; index++)
            operators[index] = &presentation->operators[index];
        qsort(operators,
              presentation->operator_count,
              sizeof(*operators),
              compare_operator_pointer);
    }
    if (presentation->rule_count > 0u) {
        rules = (RuleDecl **)malloc(presentation->rule_count * sizeof(*rules));
        if (rules == NULL) {
            free(operators);
            return set_error(error, error_cap, "out of memory sorting rules");
        }
        for (size_t index = 0u; index < presentation->rule_count; index++)
            rules[index] = &presentation->rules[index];
        qsort(rules,
              presentation->rule_count,
              sizeof(*rules),
              compare_rule_pointer);
    }

    bool ok = buffer_literal(buffer, "(gslt-presentation-v1 ") &&
              append_text(buffer, presentation->name);
    ok = ok && buffer_literal(buffer, " (signature");
    for (size_t index = 0u; ok && index < presentation->operator_count; index++) {
        char arity[32];
        int written = snprintf(arity, sizeof(arity), "%zu", operators[index]->arity);
        ok = written > 0 && (size_t)written < sizeof(arity) &&
             buffer_literal(buffer, " (operator ") &&
             append_text(buffer, operators[index]->name) &&
             buffer_byte(buffer, (uint8_t)' ') &&
             buffer_append(buffer, (const uint8_t *)arity, (size_t)written) &&
             buffer_byte(buffer, (uint8_t)')');
    }
    ok = ok && buffer_literal(buffer, ") (equations) (rewrites");
    for (size_t index = 0u; ok && index < presentation->rule_count; index++) {
        RuleDecl *rule = rules[index];
        ok = buffer_literal(buffer, " (rule ") &&
             append_text(buffer, rule->name);
        ok = ok && buffer_literal(buffer, " (head ") &&
             render_node(buffer, rule->head) &&
             buffer_literal(buffer, ") (body");
        for (size_t body = 0u; ok && body < rule->body_count; body++)
            ok = buffer_byte(buffer, (uint8_t)' ') &&
                 render_node(buffer, rule->body[body]);
        ok = ok && buffer_literal(buffer, "))");
    }
    ok = ok && buffer_literal(buffer, "))\n") && buffer_byte(buffer, 0u);
    if (ok)
        buffer->len--;
    free(rules);
    free(operators);
    if (!ok)
        return set_error(error,
                         error_cap,
                         "out of memory canonicalizing presentation");
    return true;
}

bool fhgslt_package_canonical_presentation(const FHGSLTPackage *package,
                                           size_t index,
                                           uint8_t **out,
                                           size_t *out_len,
                                           char *error,
                                           size_t error_cap) {
    if (package == NULL || out == NULL || out_len == NULL ||
        index >= package->presentation_count)
        return set_error(error, error_cap, "invalid canonicalization request");
    *out = NULL;
    *out_len = 0u;
    ByteBuffer buffer = {0};
    if (!canonical_presentation(&package->presentations[index],
                                &buffer,
                                error,
                                error_cap)) {
        buffer_destroy(&buffer);
        return false;
    }
    *out = buffer.bytes;
    *out_len = buffer.len;
    return true;
}

bool fhgslt_package_digest(const FHGSLTPackage *package,
                           char out[65],
                           char *error,
                           size_t error_cap) {
    static const uint8_t prefix[] = "FiniteHornGSLTPackageV1";
    if (package == NULL || out == NULL)
        return set_error(error, error_cap, "invalid digest request");
    Presentation **ordered = (Presentation **)malloc(
        package->presentation_count * sizeof(*ordered));
    if (ordered == NULL)
        return set_error(error, error_cap, "out of memory sorting package");
    for (size_t index = 0u; index < package->presentation_count; index++)
        ordered[index] = &package->presentations[index];
    qsort(ordered,
          package->presentation_count,
          sizeof(*ordered),
          compare_presentation_pointer);

    ByteBuffer payload = {0};
    bool ok = buffer_append(&payload, prefix, sizeof(prefix));
    for (size_t index = 0u; ok && index < package->presentation_count; index++) {
        ByteBuffer canonical = {0};
        if (!canonical_presentation(ordered[index],
                                    &canonical,
                                    error,
                                    error_cap)) {
            ok = false;
            buffer_destroy(&canonical);
            break;
        }
        if (canonical.len > UINT64_MAX) {
            ok = set_error(error,
                           error_cap,
                           "canonical presentation is too large");
            buffer_destroy(&canonical);
            break;
        }
        uint64_t length = (uint64_t)canonical.len;
        uint8_t encoded_length[8];
        for (size_t byte = 0u; byte < 8u; byte++)
            encoded_length[7u - byte] = (uint8_t)(length >> (byte * 8u));
        ok = buffer_append(&payload, encoded_length, sizeof(encoded_length)) &&
             buffer_append(&payload, canonical.bytes, canonical.len);
        buffer_destroy(&canonical);
    }
    free(ordered);
    if (!ok) {
        buffer_destroy(&payload);
        if (error == NULL || error_cap == 0u || error[0] == '\0')
            (void)set_error(error,
                            error_cap,
                            "out of memory building package digest");
        return false;
    }
    cetta_native_sha256_hex(payload.bytes, payload.len, out);
    buffer_destroy(&payload);
    return true;
}

static bool render_horn_clause_ir_rule_v1(ByteBuffer *buffer,
                                          const RuleDecl *rule) {
    bool ok = buffer_literal(buffer, "(HornClauseV1 ") &&
              render_node_with_variable_marker(
                  buffer, rule->head, (uint8_t)'$') &&
              buffer_byte(buffer, (uint8_t)' ');
    for (size_t index = 0u; ok && index < rule->body_count; index++) {
        ok = buffer_literal(buffer, "(HornBodyConsV1 ") &&
             render_node_with_variable_marker(
                 buffer, rule->body[index], (uint8_t)'$') &&
             buffer_byte(buffer, (uint8_t)' ');
    }
    ok = ok && buffer_literal(buffer, "HornBodyNilV1");
    for (size_t index = 0u; ok && index < rule->body_count; index++)
        ok = buffer_byte(buffer, (uint8_t)')');
    return ok && buffer_literal(buffer, ")\n");
}

bool fhgslt_package_horn_clause_ir_v1(const FHGSLTPackage *package,
                                      uint8_t **out,
                                      size_t *out_len,
                                      char *error,
                                      size_t error_cap) {
    if (package == NULL || out == NULL || out_len == NULL)
        return set_error(error, error_cap,
                         "invalid finite-Horn clause IR render request");
    *out = NULL;
    *out_len = 0u;

    char digest[65];
    if (!fhgslt_package_digest(package, digest, error, error_cap))
        return false;

    Presentation **presentations = (Presentation **)malloc(
        package->presentation_count * sizeof(*presentations));
    if (presentations == NULL)
        return set_error(error,
                         error_cap,
                         "out of memory sorting finite-Horn presentations");
    for (size_t index = 0u; index < package->presentation_count; index++)
        presentations[index] = &package->presentations[index];
    qsort(presentations,
          package->presentation_count,
          sizeof(*presentations),
          compare_presentation_pointer);

    ByteBuffer buffer = {0};
    bool ok = buffer_literal(
        &buffer, "(HornClauseV1 (FiniteHornPackageDigestV1 sha256-") &&
              buffer_literal(&buffer, digest) &&
              buffer_literal(&buffer, ") HornBodyNilV1)\n");
    for (size_t presentation_index = 0u;
         ok && presentation_index < package->presentation_count;
         presentation_index++) {
        Presentation *presentation = presentations[presentation_index];
        RuleDecl **rules = NULL;
        if (presentation->rule_count > 0u) {
            rules = (RuleDecl **)malloc(
                presentation->rule_count * sizeof(*rules));
            if (rules == NULL) {
                ok = false;
                break;
            }
            for (size_t rule_index = 0u;
                 rule_index < presentation->rule_count;
                 rule_index++)
                rules[rule_index] = &presentation->rules[rule_index];
            qsort(rules,
                  presentation->rule_count,
                  sizeof(*rules),
                  compare_rule_pointer);
        }
        for (size_t rule_index = 0u;
             ok && rule_index < presentation->rule_count;
             rule_index++)
            ok = render_horn_clause_ir_rule_v1(&buffer, rules[rule_index]);
        free(rules);
    }
    free(presentations);
    if (!ok) {
        buffer_destroy(&buffer);
        return set_error(error,
                         error_cap,
                         "out of memory rendering finite-Horn clause IR");
    }
    *out = buffer.bytes;
    *out_len = buffer.len;
    return true;
}

static bool presentation_source_selected(
    const Presentation *presentation,
    const char *const *sources,
    size_t source_count) {
    if (source_count == 0u)
        return true;
    for (size_t index = 0u; index < source_count; index++) {
        if (strcmp(presentation->source, sources[index]) == 0)
            return true;
    }
    return false;
}

static bool package_reflected_presentation_selected(
    const FHGSLTPackage *package,
    const char *const *sources,
    size_t source_count,
    uint8_t **out,
    size_t *out_len,
    size_t *operator_count,
    size_t *rule_count,
    char *error,
    size_t error_cap) {
    if (package == NULL || out == NULL || out_len == NULL)
        return set_error(error, error_cap, "invalid reflection request");
    if (source_count > 0u && sources == NULL)
        return set_error(error, error_cap,
                         "selected reflection omits its source table");
    *out = NULL;
    *out_len = 0u;
    if (operator_count != NULL)
        *operator_count = 0u;
    if (rule_count != NULL)
        *rule_count = 0u;

    for (size_t source_index = 0u;
         source_index < source_count;
         source_index++) {
        bool found = false;
        if (sources[source_index] == NULL)
            return set_error(error, error_cap,
                             "selected reflection contains a null source");
        for (size_t presentation_index = 0u;
             presentation_index < package->presentation_count;
             presentation_index++) {
            if (strcmp(package->presentations[presentation_index].source,
                       sources[source_index]) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            return set_error(error, error_cap,
                             "selected reflection source is outside the package: %s",
                             sources[source_index]);
    }

    char digest[65];
    if (!fhgslt_package_digest(package, digest, error, error_cap))
        return false;

    size_t selected_presentation_count = 0u;
    for (size_t index = 0u; index < package->presentation_count; index++) {
        if (presentation_source_selected(&package->presentations[index],
                                         sources,
                                         source_count))
            selected_presentation_count++;
    }
    if (selected_presentation_count == 0u)
        return set_error(error, error_cap,
                         "selected reflection contains no presentation");
    Presentation **presentations = (Presentation **)malloc(
        selected_presentation_count * sizeof(*presentations));
    if (presentations == NULL)
        return set_error(error,
                         error_cap,
                         "out of memory sorting reflected presentations");
    size_t selected_index = 0u;
    for (size_t index = 0u; index < package->presentation_count; index++) {
        if (presentation_source_selected(&package->presentations[index],
                                         sources,
                                         source_count))
            presentations[selected_index++] = &package->presentations[index];
    }
    qsort(presentations,
          selected_presentation_count,
          sizeof(*presentations),
          compare_presentation_pointer);

    ByteBuffer buffer = {0};
    bool ok = buffer_literal(&buffer,
                             "(gslt-presentation-v1 ReflectedFiniteHorn_") &&
              buffer_literal(&buffer, digest) &&
              buffer_literal(&buffer,
                             " (signature) (equations) (rewrites");
    size_t reflected_operator_index = 0u;
    size_t reflected_index = 0u;
    for (size_t presentation_index = 0u;
         ok && presentation_index < selected_presentation_count;
         presentation_index++) {
        Presentation *presentation = presentations[presentation_index];
        OperatorDecl **operators = NULL;
        RuleDecl **rules = NULL;
        if (presentation->operator_count > 0u) {
            operators = (OperatorDecl **)malloc(
                presentation->operator_count * sizeof(*operators));
            if (operators == NULL) {
                ok = false;
                break;
            }
            for (size_t operator_index = 0u;
                 operator_index < presentation->operator_count;
                 operator_index++)
                operators[operator_index] =
                    &presentation->operators[operator_index];
            qsort(operators,
                  presentation->operator_count,
                  sizeof(*operators),
                  compare_operator_pointer);
        }
        if (presentation->rule_count > 0u) {
            rules = (RuleDecl **)malloc(
                presentation->rule_count * sizeof(*rules));
            if (rules == NULL) {
                free(operators);
                ok = false;
                break;
            }
            for (size_t rule_index = 0u;
                 rule_index < presentation->rule_count;
                 rule_index++)
                rules[rule_index] = &presentation->rules[rule_index];
            qsort(rules,
                  presentation->rule_count,
                  sizeof(*rules),
                  compare_rule_pointer);
        }
        for (size_t operator_index = 0u;
             ok && operator_index < presentation->operator_count;
             operator_index++) {
            char index_text[32];
            int written = snprintf(index_text,
                                   sizeof(index_text),
                                   "%zu",
                                   reflected_operator_index);
            ok = written > 0 && (size_t)written < sizeof(index_text) &&
                 buffer_literal(&buffer, " (rule reflected-operator-") &&
                 buffer_literal(&buffer, digest) &&
                 buffer_byte(&buffer, (uint8_t)'-') &&
                 buffer_append(&buffer,
                               (const uint8_t *)index_text,
                               (size_t)written) &&
                 buffer_literal(&buffer,
                                " (head (source-operator (q-sym ") &&
                 append_text(&buffer, presentation->name) &&
                 buffer_literal(&buffer, ") (q-sym ") &&
                 append_text(&buffer, operators[operator_index]->name) &&
                 buffer_literal(&buffer, ") ") &&
                 render_quoted_index(
                     &buffer, operators[operator_index]->arity) &&
                 buffer_literal(&buffer, ")) (body))");
            reflected_operator_index++;
        }
        for (size_t rule_index = 0u;
             ok && rule_index < presentation->rule_count;
             rule_index++) {
            char index_text[32];
            int written = snprintf(index_text,
                                   sizeof(index_text),
                                   "%zu",
                                   reflected_index);
            ok = written > 0 && (size_t)written < sizeof(index_text) &&
                 buffer_literal(&buffer, " (rule reflected-") &&
                 buffer_literal(&buffer, digest) &&
                 buffer_byte(&buffer, (uint8_t)'-') &&
                 buffer_append(&buffer,
                               (const uint8_t *)index_text,
                               (size_t)written) &&
                 buffer_literal(&buffer,
                                " (head (source-rule (q-sym ") &&
                 append_text(&buffer, presentation->name) &&
                 buffer_literal(&buffer, ") ") &&
                 render_quoted_rule(&buffer, rules[rule_index]) &&
                 buffer_literal(&buffer, ")) (body))");
            reflected_index++;
        }
        free(operators);
        free(rules);
    }
    free(presentations);
    ok = ok && buffer_literal(&buffer, "))\n") &&
         buffer_byte(&buffer, 0u);
    if (!ok) {
        buffer_destroy(&buffer);
        return set_error(error,
                         error_cap,
                         "out of memory deriving reflected presentation");
    }
    buffer.len--;
    *out = buffer.bytes;
    *out_len = buffer.len;
    if (operator_count != NULL)
        *operator_count = reflected_operator_index;
    if (rule_count != NULL)
        *rule_count = reflected_index;
    return true;
}

bool fhgslt_package_reflected_presentation(const FHGSLTPackage *package,
                                           uint8_t **out,
                                           size_t *out_len,
                                           char *error,
                                           size_t error_cap) {
    return package_reflected_presentation_selected(
        package, NULL, 0u, out, out_len, NULL, NULL, error, error_cap);
}

bool fhgslt_package_reflected_sources(const FHGSLTPackage *package,
                                      const char *const *sources,
                                      size_t source_count,
                                      uint8_t **out,
                                      size_t *out_len,
                                      size_t *operator_count,
                                      size_t *rule_count,
                                      char *error,
                                      size_t error_cap) {
    if (source_count == 0u)
        return set_error(error, error_cap,
                         "selected reflection requires a source");
    return package_reflected_presentation_selected(
        package, sources, source_count, out, out_len,
        operator_count, rule_count, error, error_cap);
}
