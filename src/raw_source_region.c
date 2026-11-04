#include "raw_source_region.h"
#include "utf8.h"

#include <string.h>
#include <stdlib.h>

CettaRawSourceStatus cetta_raw_source_scan(
    const uint8_t *source, size_t length, size_t start,
    CettaRawSourceRegion *region) {
    if (!region) return CETTA_RAW_NOT_PRESENT;
    *region = (CettaRawSourceRegion){.start = start, .error_offset = start};
    if (!source || start >= length || source[start] != 'r')
        return CETTA_RAW_NOT_PRESENT;
    size_t i = start + 1u;
    while (i < length && source[i] == '#') ++i;
    size_t depth = i - start - 1u;
    if (!depth || i == length || source[i] != '"')
        return CETTA_RAW_NOT_PRESENT;
    region->fence_depth = depth;
    region->body_start = ++i;
    while (i < length) {
        if (source[i] == '"') {
            size_t j = i + 1u;
            while (j < length && j - i - 1u < depth && source[j] == '#') ++j;
            if (j - i - 1u == depth) {
                region->body_end = i;
                region->end = j;
                region->error_offset = j;
                return CETTA_RAW_COMPLETE;
            }
        }
        uint32_t scalar, width;
        if (!cetta_utf8_decode_one(source, length, i, &scalar, &width)) {
            region->error_offset = i;
            return cetta_utf8_incomplete_prefix(source + i, length - i)
                ? CETTA_RAW_INCOMPLETE : CETTA_RAW_INVALID_UTF8;
        }
        i += width;
    }
    region->error_offset = length;
    return CETTA_RAW_INCOMPLETE;
}

bool cetta_raw_source_depth(const uint8_t *body, size_t length,
                            size_t preferred, size_t *depth) {
    if ((!body && length) || !depth) return false;
    size_t selected = preferred ? preferred : 1u;
    for (size_t i = 0; i < length;) {
        uint32_t scalar, width;
        if (!cetta_utf8_decode_one(body, length, i, &scalar, &width)) return false;
        if (scalar == '"') {
            size_t j = i + 1u;
            while (j < length && body[j] == '#') ++j;
            /* A run of k hashes after a quote collides with every depth
             * from 1 through k, including a prefix of a longer run. */
            size_t needed = j - i;
            if (needed > selected) selected = needed;
            i = j;
        } else {
            i += width;
        }
    }
    *depth = selected;
    return true;
}

bool cetta_raw_source_render(const uint8_t *body, size_t length,
                             size_t preferred, uint8_t *output,
                             size_t capacity, size_t *written) {
    size_t depth;
    if (!written || !cetta_raw_source_depth(body, length, preferred, &depth) ||
        length > SIZE_MAX - 3u || depth > (SIZE_MAX - length - 3u) / 2u)
        return false;
    size_t total = length + 3u + 2u * depth;
    if (output && capacity < total) return false;
    *written = total;
    if (!output) return true;
    output[0] = 'r';
    memset(output + 1u, '#', depth);
    output[depth + 1u] = '"';
    if (length) memcpy(output + depth + 2u, body, length);
    output[depth + 2u + length] = '"';
    memset(output + depth + 3u + length, '#', depth);
    return true;
}

CettaSourceDocumentStatus cetta_source_document_scan(const uint8_t *s,size_t n,
    bool raw_enabled,CettaRawSourceVisitor visit,void *context,size_t *error) {
    if(error)*error=0;
    if(!s && n)return CETTA_SOURCE_DOCUMENT_INVALID;
    uint8_t *stack=NULL;size_t depth=0,capacity=0;bool boundary=true;
    CettaSourceDocumentStatus result=CETTA_SOURCE_DOCUMENT_COMPLETE;
    for(size_t i=0;i<n;) {
        if(error)*error=i;
        uint8_t c=s[i];
        if(c==';') {
            while(i<n && s[i]!='\n')i++;
            boundary=true;continue;
        }
        if(c==' '||c=='\t'||c=='\r'||c=='\n'){i++;boundary=true;continue;}
        if(boundary && raw_enabled) {
            CettaRawSourceRegion region;
            CettaRawSourceStatus status=cetta_raw_source_scan(s,n,i,&region);
            if(status==CETTA_RAW_INCOMPLETE||status==CETTA_RAW_INVALID_UTF8){
                if(error)*error=region.error_offset;
                region.body_end=n;region.end=n;
                if(visit&&!visit(context,&region,status)){result=CETTA_SOURCE_DOCUMENT_ALLOCATION_FAILED;goto done;}
                result=status==CETTA_RAW_INCOMPLETE?CETTA_SOURCE_DOCUMENT_INCOMPLETE:CETTA_SOURCE_DOCUMENT_INVALID;
                goto done;
            }
            if(status==CETTA_RAW_COMPLETE){
                if(visit&&!visit(context,&region,status)){result=CETTA_SOURCE_DOCUMENT_ALLOCATION_FAILED;goto done;}
                i=region.end;boundary=true;continue;
            }
        }
        if(c=='"') {
            bool closed=false;i++;
            while(i<n) {
                if(s[i]=='"'){i++;closed=true;break;}
                if(s[i]=='\\') {
                    if(++i==n)break;
                }
                uint32_t scalar,width;
                if(!cetta_utf8_decode_one(s,n,i,&scalar,&width)) {
                    if(error)*error=i;
                    result=cetta_utf8_incomplete_prefix(s+i,n-i)?CETTA_SOURCE_DOCUMENT_INCOMPLETE:CETTA_SOURCE_DOCUMENT_INVALID;
                    goto done;
                }
                i+=width;
            }
            if(!closed){if(error)*error=n;result=CETTA_SOURCE_DOCUMENT_INCOMPLETE;goto done;}
            boundary=true;continue;
        }
        if(c=='('||c=='[') {
            if(depth==capacity) {
                size_t next=capacity?capacity*2u:16u;
                if(next<capacity){result=CETTA_SOURCE_DOCUMENT_ALLOCATION_FAILED;goto done;}
                uint8_t *grown=realloc(stack,next);
                if(!grown){result=CETTA_SOURCE_DOCUMENT_ALLOCATION_FAILED;goto done;}
                stack=grown;capacity=next;
            }
            stack[depth++]=c;i++;boundary=true;continue;
        }
        if(c==')'||c==']') {
            if(!depth || stack[depth-1u]!=(c==')'?'(':'[')){result=CETTA_SOURCE_DOCUMENT_INVALID;goto done;}
            depth--;i++;boundary=true;continue;
        }
        uint32_t scalar,width;
        if(!cetta_utf8_decode_one(s,n,i,&scalar,&width)) {
            result=cetta_utf8_incomplete_prefix(s+i,n-i)?CETTA_SOURCE_DOCUMENT_INCOMPLETE:CETTA_SOURCE_DOCUMENT_INVALID;
            goto done;
        }
        i+=width;boundary=false;
    }
    if(error)*error=n;
    if(depth)result=CETTA_SOURCE_DOCUMENT_INCOMPLETE;
done:
    free(stack);return result;
}
