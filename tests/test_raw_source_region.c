#include "src/raw_source_region.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void roundtrip(const uint8_t *body, size_t length, size_t preferred) {
    size_t needed, depth;
    assert(cetta_raw_source_depth(body, length, preferred, &depth));
    assert(depth >= (preferred ? preferred : 1u));
    assert(cetta_raw_source_render(body, length, preferred, NULL, 0, &needed));
    uint8_t *text = malloc(needed + 4u);
    assert(text);
    memset(text, 0xa5, needed + 4u);
    assert(!cetta_raw_source_render(body, length, preferred, text, needed - 1u, &needed));
    assert(text[0] == 0xa5);
    assert(cetta_raw_source_render(body, length, preferred, text + 2, needed, &needed));
    CettaRawSourceRegion region;
    assert(cetta_raw_source_scan(text, needed + 4u, 2u, &region) == CETTA_RAW_COMPLETE);
    assert(region.start == 2u && region.end == needed + 2u);
    assert(region.body_end - region.body_start == length);
    assert(!length || !memcmp(text + region.body_start, body, length));
    assert(region.fence_depth == depth);
    size_t full_end = region.end;
    for (size_t end = region.body_start; end < full_end; ++end)
        assert(cetta_raw_source_scan(text, end, 2u, &region) == CETTA_RAW_INCOMPLETE);
    free(text);
}

static bool count_region(void *context,const CettaRawSourceRegion *r,CettaRawSourceStatus status) {
    size_t *count=context;
    assert(r->start<r->body_start && r->body_start<=r->body_end && r->body_end<r->end);
    assert(status==CETTA_RAW_COMPLETE);
    ++*count;return true;
}

int main(void) {
    const char *cases[] = {"", "hello", "\\n\\t", "\"", "\"#", "\"###",
        "r#\"guest\"#", "\r\n  def f():\n    return \"value\"\n",
        "(guest ; ) # \" \"\"\")", "\xe2\x88\x80\xf0\x9f\x8c\xb1"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i)
        for (size_t depth = 0; depth < 6; ++depth)
            roundtrip((const uint8_t *)cases[i], strlen(cases[i]), depth);
    const uint8_t controls[] = {'a', 0, 'b', 1, 127, '\r', '\n'};
    roundtrip(controls, sizeof(controls), 1);
    CettaRawSourceRegion region;
    const uint8_t invalid[] = {'r','#','"',0xc0,0xaf,'"','#'};
    assert(cetta_raw_source_scan(invalid, sizeof(invalid), 0, &region) == CETTA_RAW_INVALID_UTF8);
    assert(region.error_offset == 3);
    size_t count = 123;
    assert(!cetta_raw_source_render(invalid + 3, 2, 1, NULL, 0, &count));
    assert(count == 123);
    assert(!cetta_raw_source_render(NULL, 0, SIZE_MAX, NULL, 0, &count));
    assert(cetta_raw_source_scan((const uint8_t *)"r##word", 7, 0, &region) == CETTA_RAW_NOT_PRESENT);
    assert(cetta_raw_source_scan((const uint8_t *)"r#", 2, 0, &region) == CETTA_RAW_NOT_PRESENT);
    assert(cetta_raw_source_scan((const uint8_t *)"r#\"\"##", 6, 0, &region) == CETTA_RAW_COMPLETE);
    assert(region.end == 5); /* extra hash belongs to the host */
    const char *complete[] = {"(f r#\"( ] ; \\\" guest\"#)",
        "; r#\"comment\n(f \"r#\\\"quoted\")", "(f [r##\"r#\"body\"#\"##])",
        "!(f\n r#\"body\r\n\"#)", "r#\"first\"#r##\"second\"##"};
    size_t expected[]={1,0,1,1,2};
    for(size_t i=0;i<sizeof(complete)/sizeof(*complete);i++) {
        size_t found=0,error=0;
        assert(cetta_source_document_scan((const uint8_t *)complete[i],strlen(complete[i]),true,
            count_region,&found,&error)==CETTA_SOURCE_DOCUMENT_COMPLETE);
        assert(found==expected[i]);
    }
    const char *unfinished[]={"(f r#\"body", "(f r##\"wrong\"#)","(f", "\"text\\"};
    for(size_t i=0;i<sizeof(unfinished)/sizeof(*unfinished);i++) {
        size_t error=0;
        assert(cetta_source_document_scan((const uint8_t *)unfinished[i],strlen(unfinished[i]),true,
            NULL,NULL,&error)==CETTA_SOURCE_DOCUMENT_INCOMPLETE);
    }
    const char *bad[]={")", "([)]", "(f]\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        size_t error=0;
        assert(cetta_source_document_scan((const uint8_t *)bad[i],strlen(bad[i]),true,
            NULL,NULL,&error)==CETTA_SOURCE_DOCUMENT_INVALID);
    }
    return 0;
}
