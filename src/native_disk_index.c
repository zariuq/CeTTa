#define _POSIX_C_SOURCE 200809L
#include "native_disk_index.h"
#include "native_journal.h"
#include "native_sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A compressed binary radix tree. Nine-bit symbols (byte+1, terminator=0)
 * give arbitrary byte keys, including prefixes and embedded NULs, their usual
 * lexicographic order. Critical bit numbers strictly increase down a path;
 * depth is bounded by key size rather than archive size. Only a changed path
 * is copied. Each child/value precedes its parent, so offsets also prohibit
 * cycles. All frames use the shared native journal's checksums/framing.
 */
enum { BRANCH=1, LEAF=2, VALUE=3, FRAME=72 };
typedef struct {
    bool leaf;
    uint64_t bit, left, right, value;
    CettaDiskBuffer key;
} Node;
typedef struct { uint64_t reference, bit, left, right; bool rightward; } Path;
typedef struct { uint64_t reference, parent; bool has_parent; } Pending;
struct CettaDiskCursor {
    CettaDiskRoot root;
    Pending *pending;
    size_t count, capacity;
    CettaDiskBuffer upper;
    bool failed, has_upper;
};

static uint64_t number(const unsigned char *p,size_t n) {
    uint64_t value=0;
    for (size_t i=0;i<n;++i) value=(value<<8)|p[i];
    return value;
}
static void put_number(unsigned char *p,size_t n,uint64_t value) {
    for (size_t i=n;i>0;--i) { p[i-1]=(unsigned char)value; value>>=8; }
}
static bool valid(const CettaDiskRoot *root) {
    return root && root->fd>=0 && root->end<=INT64_MAX &&
        (!root->root || root->root<=root->end) &&
        root->key_limit<=UINT32_MAX-13u && root->value_limit<=UINT32_MAX-1u;
}
static bool bytes_valid(CettaDiskBytes b,size_t limit) {
    return b.size<=limit && (!b.size || b.data);
}
static CettaDiskStatus journal_status(CettaJournalStatus status) {
    if (status==CETTA_JOURNAL_OK) return CETTA_DISK_OK;
    if (status==CETTA_JOURNAL_NOMEM) return CETTA_DISK_NOMEM;
    if (status==CETTA_JOURNAL_IO || status==CETTA_JOURNAL_UNKNOWN) return CETTA_DISK_IO;
    return CETTA_DISK_CORRUPT;
}
static CettaDiskStatus read_all(int fd,unsigned char *data,size_t size,uint64_t offset) {
    while (size) {
        size_t amount=size>(size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : size;
        ssize_t n=pread(fd,data,amount,(off_t)offset);
        if (n<0 && errno==EINTR) continue;
        if (n<0) return CETTA_DISK_IO;
        if (!n) return CETTA_DISK_CORRUPT;
        data+=n; size-=(size_t)n; offset+=(uint64_t)n;
    }
    return CETTA_DISK_OK;
}
static CettaDiskStatus header(const CettaDiskRoot *root,uint64_t reference,
                              unsigned char bytes[FRAME],size_t *size) {
    if (!reference || reference>root->end || root->end-reference+1<FRAME)
        return CETTA_DISK_CORRUPT;
    CettaDiskStatus status=read_all(root->fd,bytes,FRAME,reference-1);
    if (status!=CETTA_DISK_OK) return status;
    if (memcmp(bytes,"CSL1",4)) return CETTA_DISK_CORRUPT;
    *size=(size_t)number(bytes+4,4);
    if (*size>root->end-reference+1-FRAME) return CETTA_DISK_CORRUPT;
    return CETTA_DISK_OK;
}
void cetta_disk_buffer_free(CettaDiskBuffer *buffer) {
    if (buffer) { free(buffer->data); *buffer=(CettaDiskBuffer){0}; }
}
static void node_free(Node *node) { cetta_disk_buffer_free(&node->key); }
static CettaDiskStatus node_read(const CettaDiskRoot *root,uint64_t reference,Node *node) {
    *node=(Node){0};
    if (!reference || reference>root->end) return CETTA_DISK_CORRUPT;
    CettaJournalRecord row={0};
    size_t limit=root->key_limit+13;
    if (limit<25) limit=25;
    CettaDiskStatus status=journal_status(cetta_journal_read(root->fd,reference-1,
        root->end,limit,&row));
    if (status!=CETTA_DISK_OK) return status;
    status=CETTA_DISK_CORRUPT;
    if (row.size==25 && row.data[0]==BRANCH) {
        node->bit=number(row.data+1,8); node->left=number(row.data+9,8);
        node->right=number(row.data+17,8);
        if (node->bit<((uint64_t)root->key_limit+1)*9 && node->left && node->right &&
            node->left<reference && node->right<reference && node->left!=node->right)
            status=CETTA_DISK_OK;
    } else if (row.size>=13 && row.data[0]==LEAF) {
        size_t size=(size_t)number(row.data+1,4); node->value=number(row.data+5,8);
        if (size==row.size-13 && size<=root->key_limit && node->value && node->value<reference) {
            node->key.data=malloc(size ? size : 1);
            if (!node->key.data) status=CETTA_DISK_NOMEM;
            else {
                memcpy(node->key.data,row.data+13,size); node->key.size=size;
                node->leaf=true; status=CETTA_DISK_OK;
            }
        }
    }
    cetta_journal_record_free(&row);
    return status;
}
static unsigned key_bit(CettaDiskBytes key,uint64_t bit) {
    uint64_t index=bit/9;
    unsigned value=index<key.size ? (unsigned)key.data[index]+1 : 0;
    return (value>>(8-bit%9))&1;
}
static bool key_equal(CettaDiskBytes left,CettaDiskBytes right) {
    return left.size==right.size && (!left.size || !memcmp(left.data,right.data,left.size));
}
static int key_compare(CettaDiskBytes left,CettaDiskBytes right) {
    size_t common=left.size<right.size ? left.size : right.size;
    int order=common ? memcmp(left.data,right.data,common) : 0;
    return order ? order : left.size<right.size ? -1 : left.size>right.size;
}
static uint64_t first_bit(CettaDiskBytes left,CettaDiskBytes right) {
    size_t at=0,common=left.size<right.size ? left.size : right.size;
    while (at<common && left.data[at]==right.data[at]) ++at;
    unsigned l=at<left.size ? (unsigned)left.data[at]+1 : 0;
    unsigned r=at<right.size ? (unsigned)right.data[at]+1 : 0;
    unsigned difference=l^r,shift=0;
    while (!(difference&(256u>>shift))) ++shift;
    return (uint64_t)at*9+shift;
}
static CettaDiskStatus grow(void **data,size_t *capacity,size_t need,size_t width,uint64_t limit) {
    if (need>*capacity) {
        if (need>limit || need>SIZE_MAX/width) return CETTA_DISK_LIMIT;
        size_t next=*capacity ? *capacity : 16;
        while (next<need && next<=SIZE_MAX/2) next*=2;
        if (next>limit) next=(size_t)limit;
        if (next>SIZE_MAX/width) return CETTA_DISK_LIMIT;
        void *fresh=realloc(*data,next*width);
        if (!fresh) return CETTA_DISK_NOMEM;
        *data=fresh; *capacity=next;
    }
    return CETTA_DISK_OK;
}
static CettaDiskStatus blob_info(const CettaDiskRoot *root,uint64_t reference,CettaDiskBlob *out) {
    unsigned char bytes[FRAME],tag; size_t size;
    CettaDiskStatus status=header(root,reference,bytes,&size);
    if (status!=CETTA_DISK_OK) return status;
    if (!size || size-1>root->value_limit) return CETTA_DISK_CORRUPT;
    status=read_all(root->fd,&tag,1,reference-1+FRAME);
    if (status!=CETTA_DISK_OK) return status;
    if (tag!=VALUE) return CETTA_DISK_CORRUPT;
    *out=(CettaDiskBlob){reference,size-1}; return CETTA_DISK_OK;
}
CettaDiskStatus cetta_disk_lookup(const CettaDiskRoot *root,CettaDiskBytes key,CettaDiskBlob *value) {
    if (!value) return CETTA_DISK_INVALID;
    *value=(CettaDiskBlob){0};
    if (!valid(root) || !bytes_valid(key,root->key_limit)) return CETTA_DISK_INVALID;
    uint64_t reference=root->root,parent=0; bool has_parent=false;
    while (reference) {
        Node node; CettaDiskStatus status=node_read(root,reference,&node);
        if (status!=CETTA_DISK_OK) return status;
        if (node.leaf) {
            bool matches=key_equal(key,(CettaDiskBytes){node.key.data,node.key.size});
            uint64_t blob=node.value; node_free(&node);
            return matches ? blob_info(root,blob,value) : CETTA_DISK_ABSENT;
        }
        if (has_parent && node.bit<=parent) return CETTA_DISK_CORRUPT;
        parent=node.bit; has_parent=true;
        reference=key_bit(key,node.bit) ? node.right : node.left;
    }
    return CETTA_DISK_ABSENT;
}
CettaDiskStatus cetta_disk_blob_read(const CettaDiskRoot *root,CettaDiskBlob value,
        uint64_t offset,void *destination,size_t capacity,size_t *written) {
    if (!written) return CETTA_DISK_INVALID;
    *written=0;
    if (!valid(root) || offset>value.size || (capacity && !destination)) return CETTA_DISK_INVALID;
    unsigned char bytes[FRAME],buffer[32768]; size_t size;
    CettaDiskStatus status=header(root,value.reference,bytes,&size);
    if (status!=CETTA_DISK_OK) return status;
    if (!size || size-1!=value.size || value.size>root->value_limit) return CETTA_DISK_CORRUPT;
    size_t wanted=value.size-offset<capacity ? (size_t)(value.size-offset) : capacity;
    CettaNativeSha256 hash; cetta_native_sha256_init(&hash);
    for (size_t done=0;done<size;) {
        size_t amount=size-done;
        if (amount>sizeof(buffer)) amount=sizeof(buffer);
        status=read_all(root->fd,buffer,amount,value.reference-1+FRAME+done);
        if (status!=CETTA_DISK_OK) return status;
        if (!done && buffer[0]!=VALUE) return CETTA_DISK_CORRUPT;
        cetta_native_sha256_update(&hash,buffer,amount);
        uint64_t start=(uint64_t)done>offset+1 ? (uint64_t)done : offset+1;
        uint64_t end=(uint64_t)done+amount<offset+1+wanted ? (uint64_t)done+amount : offset+1+wanted;
        if (end>start) memcpy((unsigned char *)destination+(size_t)(start-offset-1),
                             buffer+(size_t)(start-done),(size_t)(end-start));
        done+=amount;
    }
    char digest[65]; cetta_native_sha256_finish_hex(&hash,digest);
    if (memcmp(digest,bytes+8,64)) return CETTA_DISK_CORRUPT;
    *written=wanted; return CETTA_DISK_OK;
}
static CettaDiskStatus append(CettaDiskRoot *root,const unsigned char *data,size_t size,uint64_t *reference) {
    if (root->end>INT64_MAX-FRAME-size) return CETTA_DISK_LIMIT;
    uint64_t start=root->end;
    CettaDiskStatus status=journal_status(cetta_journal_append(root->fd,data,size,false,&root->end));
    if (status==CETTA_DISK_OK) *reference=start+1;
    return status;
}
static CettaDiskStatus branch(CettaDiskRoot *root,uint64_t bit,uint64_t left,uint64_t right,uint64_t *out) {
    unsigned char data[25]={BRANCH};
    put_number(data+1,8,bit); put_number(data+9,8,left); put_number(data+17,8,right);
    return append(root,data,sizeof(data),out);
}
static CettaDiskStatus leaf(CettaDiskRoot *root,CettaDiskBytes key,uint64_t value,uint64_t *out) {
    unsigned char *data=malloc(13+key.size);
    if (!data) return CETTA_DISK_NOMEM;
    data[0]=LEAF; put_number(data+1,4,key.size); put_number(data+5,8,value);
    if (key.size) memcpy(data+13,key.data,key.size);
    CettaDiskStatus status=append(root,data,13+key.size,out); free(data); return status;
}
/* The caller already owns the body; shared framing accepts borrowed parts. */
static CettaDiskStatus value_append(CettaDiskRoot *root,CettaDiskBytes value,uint64_t *out) {
    unsigned char tag=VALUE;
    uint64_t size=(uint64_t)value.size+1;
    if (root->end>INT64_MAX-FRAME-size) return CETTA_DISK_LIMIT;
    uint64_t start=root->end;
    CettaJournalPart parts[]={{&tag,1},{value.data,value.size}};
    CettaDiskStatus status=journal_status(cetta_journal_append_parts(root->fd,parts,2,false,&root->end));
    if (status==CETTA_DISK_OK) *out=start+1;
    return status;
}
static CettaDiskStatus change(CettaDiskRoot *root,const CettaDiskOp *op) {
    Path *path=NULL; size_t count=0,capacity=0;
    uint64_t reference=root->root,parent=0; bool has_parent=false;
    Node found={0}; CettaDiskStatus status=CETTA_DISK_OK;
    while (reference) {
        status=node_read(root,reference,&found);
        if (status!=CETTA_DISK_OK || found.leaf) break;
        if (has_parent && found.bit<=parent) { status=CETTA_DISK_CORRUPT; break; }
        status=grow((void **)&path,&capacity,count+1,sizeof(*path),((uint64_t)root->key_limit+1)*9);
        if (status!=CETTA_DISK_OK) break;
        bool side=key_bit(op->key,found.bit)!=0;
        path[count++]=(Path){reference,found.bit,found.left,found.right,side};
        parent=found.bit; has_parent=true; reference=side ? found.right : found.left;
    }
    if (status!=CETTA_DISK_OK) goto done;
    bool matches=reference && key_equal(op->key,(CettaDiskBytes){found.key.data,found.key.size});
    uint64_t replacement=0;
    size_t cut=count;
    if (op->remove) {
        if (!matches) goto done;
        if (count) { Path last=path[--cut]; replacement=last.rightward ? last.left : last.right; }
    } else {
        uint64_t body,new_leaf;
        status=value_append(root,op->value,&body);
        if (status!=CETTA_DISK_OK) goto done;
        status=leaf(root,op->key,body,&new_leaf);
        if (status!=CETTA_DISK_OK) goto done;
        replacement=new_leaf;
        if (reference && !matches) {
            uint64_t bit=first_bit(op->key,(CettaDiskBytes){found.key.data,found.key.size});
            cut=0;
            while (cut<count && path[cut].bit<bit) ++cut;
            uint64_t old=cut<count ? path[cut].reference : reference;
            bool side=key_bit(op->key,bit)!=0;
            status=branch(root,bit,side ? old : new_leaf,side ? new_leaf : old,&replacement);
            if (status!=CETTA_DISK_OK) goto done;
        }
    }
    while (cut) {
        Path step=path[--cut];
        status=branch(root,step.bit,step.rightward ? step.left : replacement,
                      step.rightward ? replacement : step.right,&replacement);
        if (status!=CETTA_DISK_OK) goto done;
    }
    root->root=replacement;
done:
    node_free(&found); free(path); return status;
}
CettaDiskStatus cetta_disk_build(const CettaDiskRoot *base,const CettaDiskOp *operations,
                                size_t count,CettaDiskRoot *candidate) {
    if (!candidate || !base) return CETTA_DISK_INVALID;
    CettaDiskRoot captured=*base;
    if (candidate!=base) *candidate=(CettaDiskRoot){.fd=-1};
    base=&captured;
    if (!valid(base) || (count && !operations)) return CETTA_DISK_INVALID;
    for (size_t i=0;i<count;++i) if (!bytes_valid(operations[i].key,base->key_limit) ||
        (!operations[i].remove && !bytes_valid(operations[i].value,base->value_limit)))
        return CETTA_DISK_INVALID;
    off_t end=lseek(base->fd,0,SEEK_END);
    if (end<0) return CETTA_DISK_IO;
    if ((uint64_t)end<base->end) return CETTA_DISK_CORRUPT;
    CettaDiskRoot draft=*base; draft.end=(uint64_t)end;
    for (size_t i=0;i<count;++i) {
        CettaDiskStatus status=change(&draft,&operations[i]);
        if (status!=CETTA_DISK_OK) return status;
    }
    *candidate=draft; return CETTA_DISK_OK;
}
CettaDiskStatus cetta_disk_sync(const CettaDiskRoot *candidate) {
    if (!valid(candidate)) return CETTA_DISK_INVALID;
    int result;
    do { result=fsync(candidate->fd); } while (result<0 && errno==EINTR);
    return result ? CETTA_DISK_IO : CETTA_DISK_OK;
}
static CettaDiskStatus push(CettaDiskCursor *cursor,uint64_t reference,uint64_t parent,bool has_parent) {
    CettaDiskStatus status=grow((void **)&cursor->pending,&cursor->capacity,cursor->count+1,
                               sizeof(*cursor->pending),((uint64_t)cursor->root.key_limit+1)*9+1);
    if (status==CETTA_DISK_OK) cursor->pending[cursor->count++]=(Pending){reference,parent,has_parent};
    return status;
}
static CettaDiskStatus seek(CettaDiskCursor *cursor,CettaDiskBytes lower,bool exclusive) {
    Path *path=NULL; size_t count=0,capacity=0;
    uint64_t reference=cursor->root.root,parent=0; bool has_parent=false;
    Node node={0}; CettaDiskStatus status=CETTA_DISK_OK;
    while (reference) {
        status=node_read(&cursor->root,reference,&node);
        if (status!=CETTA_DISK_OK || node.leaf) break;
        if (has_parent && node.bit<=parent) { status=CETTA_DISK_CORRUPT; break; }
        status=grow((void **)&path,&capacity,count+1,sizeof(*path),((uint64_t)cursor->root.key_limit+1)*9);
        if (status!=CETTA_DISK_OK) break;
        bool side=key_bit(lower,node.bit)!=0;
        path[count++]=(Path){reference,node.bit,node.left,node.right,side};
        parent=node.bit; has_parent=true; reference=side ? node.right : node.left;
    }
    if (status!=CETTA_DISK_OK || !reference) goto done;
    CettaDiskBytes found={node.key.data,node.key.size};
    int order=key_compare(lower,found); size_t cut=count;
    if (order) {
        uint64_t different=first_bit(lower,found); cut=0;
        while (cut<count && path[cut].bit<different) ++cut;
    }
    /* Above the first differing bit, retain later right subtrees. At/below
     * it, every key in the selected subtree lies on the same side of lower.
     * This handles a bound differing before any stored critical bit too. */
    for (size_t i=0;i<cut && status==CETTA_DISK_OK;++i)
        if (!path[i].rightward) status=push(cursor,path[i].right,path[i].bit,true);
    if (status==CETTA_DISK_OK && (order<0 || (!order && !exclusive))) {
        uint64_t target=cut<count ? path[cut].reference : reference;
        status=push(cursor,target,cut ? path[cut-1].bit : 0,cut!=0);
    }
done:
    node_free(&node); free(path); return status;
}
CettaDiskStatus cetta_disk_cursor_open_range(const CettaDiskRoot *root,
        const CettaDiskBytes *lower,bool exclusive,const CettaDiskBytes *upper,CettaDiskCursor **out) {
    if (!out) return CETTA_DISK_INVALID;
    *out=NULL;
    if (!valid(root) || (lower && !bytes_valid(*lower,root->key_limit)) ||
        (upper && !bytes_valid(*upper,root->key_limit))) return CETTA_DISK_INVALID;
    CettaDiskCursor *cursor=calloc(1,sizeof(*cursor));
    if (!cursor) return CETTA_DISK_NOMEM;
    cursor->root=*root;
    cursor->root.fd=fcntl(root->fd,F_DUPFD_CLOEXEC,0);
    if (cursor->root.fd<0) { free(cursor); return CETTA_DISK_IO; }
    if (upper) {
        cursor->upper.data=malloc(upper->size ? upper->size : 1);
        if (!cursor->upper.data) { cetta_disk_cursor_close(cursor); return CETTA_DISK_NOMEM; }
        if (upper->size) memcpy(cursor->upper.data,upper->data,upper->size);
        cursor->upper.size=upper->size; cursor->has_upper=true;
    }
    CettaDiskStatus status=CETTA_DISK_OK;
    if (lower && upper && key_compare(*lower,*upper)>=0) { /* Empty interval. */ }
    else if (lower) status=seek(cursor,*lower,exclusive);
    else if (root->root) status=push(cursor,root->root,0,false);
    if (status!=CETTA_DISK_OK) { cetta_disk_cursor_close(cursor); return status; }
    *out=cursor; return CETTA_DISK_OK;
}
CettaDiskStatus cetta_disk_cursor_open(const CettaDiskRoot *root,CettaDiskCursor **out) {
    return cetta_disk_cursor_open_range(root,NULL,false,NULL,out);
}
CettaDiskStatus cetta_disk_cursor_next(CettaDiskCursor *cursor,CettaDiskBuffer *key,CettaDiskBlob *value) {
    if (!key || !value) return CETTA_DISK_INVALID;
    *key=(CettaDiskBuffer){0}; *value=(CettaDiskBlob){0};
    if (!cursor || cursor->failed) return CETTA_DISK_INVALID;
    while (cursor->count) {
        Pending pending=cursor->pending[--cursor->count]; Node node;
        CettaDiskStatus status=node_read(&cursor->root,pending.reference,&node);
        if (status!=CETTA_DISK_OK) { cursor->failed=true; return status; }
        if (node.leaf) {
            if (cursor->has_upper && key_compare((CettaDiskBytes){node.key.data,node.key.size},
                    (CettaDiskBytes){cursor->upper.data,cursor->upper.size})>=0) {
                node_free(&node); cursor->count=0; return CETTA_DISK_END;
            }
            status=blob_info(&cursor->root,node.value,value);
            if (status!=CETTA_DISK_OK) { node_free(&node); cursor->failed=true; return status; }
            *key=node.key; return CETTA_DISK_OK;
        }
        if (pending.has_parent && node.bit<=pending.parent) { cursor->failed=true; return CETTA_DISK_CORRUPT; }
        status=push(cursor,node.right,node.bit,true);
        if (status==CETTA_DISK_OK) status=push(cursor,node.left,node.bit,true);
        if (status!=CETTA_DISK_OK) { cursor->failed=true; return status; }
    }
    return CETTA_DISK_END;
}
const CettaDiskRoot *cetta_disk_cursor_root(const CettaDiskCursor *cursor) { return cursor ? &cursor->root : NULL; }
void cetta_disk_cursor_close(CettaDiskCursor *cursor) {
    if (cursor) {
        close(cursor->root.fd); free(cursor->pending);
        cetta_disk_buffer_free(&cursor->upper); free(cursor);
    }
}

typedef struct { uint64_t reference, parent, bit, right, left_copy; unsigned phase; bool has_parent; } Copy;
CettaDiskStatus cetta_disk_compact(const CettaDiskRoot *source,int destination,CettaDiskRoot *candidate) {
    if (!candidate || !source) return CETTA_DISK_INVALID;
    CettaDiskRoot captured=*source;
    if (candidate!=source) *candidate=(CettaDiskRoot){.fd=-1};
    source=&captured;
    if (!valid(source) || destination<0) return CETTA_DISK_INVALID;
    struct stat from,to;
    if (fstat(source->fd,&from) || fstat(destination,&to)) return CETTA_DISK_IO;
    if (to.st_size || (from.st_dev==to.st_dev && from.st_ino==to.st_ino)) return CETTA_DISK_INVALID;
    if (lseek(destination,0,SEEK_SET)!=0) return CETTA_DISK_IO;
    CettaDiskRoot draft=*source; draft.fd=destination; draft.root=0; draft.end=0;
    Copy *stack=NULL; size_t count=0,capacity=0; uint64_t result=0;
    CettaDiskStatus status=CETTA_DISK_OK;
    if (source->root) {
        status=grow((void **)&stack,&capacity,1,sizeof(*stack),((uint64_t)source->key_limit+1)*9+1);
        if (status!=CETTA_DISK_OK) return status;
        stack[count++]=(Copy){.reference=source->root};
    }
    while (count) {
        Copy *top=&stack[count-1];
        if (top->phase==2) {
            status=branch(&draft,top->bit,top->left_copy,result,&result);
            if (status!=CETTA_DISK_OK) break;
            --count; continue;
        }
        if (top->phase==1) {
            top->left_copy=result; top->phase=2;
            uint64_t child=top->right,bit=top->bit;
            status=grow((void **)&stack,&capacity,count+1,sizeof(*stack),((uint64_t)source->key_limit+1)*9+1);
            if (status!=CETTA_DISK_OK) break;
            stack[count++]=(Copy){.reference=child,.parent=bit,.has_parent=true}; continue;
        }
        Node node; status=node_read(source,top->reference,&node);
        if (status!=CETTA_DISK_OK) break;
        if (node.leaf) {
            CettaDiskBlob body; status=blob_info(source,node.value,&body);
            uint64_t copied=draft.end+1;
            if (status==CETTA_DISK_OK) status=journal_status(cetta_journal_copy(source->fd,body.reference-1,
                source->end,source->value_limit+1,destination,&draft.end));
            if (status==CETTA_DISK_OK) status=leaf(&draft,(CettaDiskBytes){node.key.data,node.key.size},copied,&result);
            node_free(&node);
            if (status!=CETTA_DISK_OK) break;
            --count; continue;
        }
        if (top->has_parent && node.bit<=top->parent) { status=CETTA_DISK_CORRUPT; break; }
        top->phase=1; top->bit=node.bit; top->right=node.right;
        status=grow((void **)&stack,&capacity,count+1,sizeof(*stack),((uint64_t)source->key_limit+1)*9+1);
        if (status!=CETTA_DISK_OK) break;
        stack[count++]=(Copy){.reference=node.left,.parent=node.bit,.has_parent=true};
    }
    free(stack);
    if (status==CETTA_DISK_OK) { draft.root=result; *candidate=draft; }
    return status;
}
