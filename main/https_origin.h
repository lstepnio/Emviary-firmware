#ifndef HTTPS_ORIGIN_H
#define HTTPS_ORIGIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

typedef struct { char host[256]; unsigned port; } https_origin_t;
static inline bool https_origin_parse(const char *url, https_origin_t *out)
{
    if (!url || strncasecmp(url,"https://",8)!=0) return false;
    const char *start=url+8, *end=start;
    while (*end && *end!='/' && *end!='?' && *end!='#') ++end;
    if (end==start || (size_t)(end-start)>=sizeof(out->host)) return false;
    const char *port=NULL, *host_end=end;
    if (*start=='[') {
        const char *bracket=(const char *)memchr(start,']',(size_t)(end-start));
        if (!bracket || bracket==start+1) return false;
        host_end=bracket+1;
        if (host_end<end) { if (*host_end!=':') return false; port=host_end+1; }
    } else {
        const char *colon=(const char *)memchr(start,':',(size_t)(end-start));
        if (colon) { host_end=colon; port=colon+1; }
    }
    if (host_end==start) return false;
    for (const char *p=start; p<host_end; ++p) {
        unsigned char c=*p;
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
              c=='.'||c=='-'||c==':'||c=='['||c==']')) return false;
    }
    out->port=443;
    if (port) {
        unsigned value=0;
        if (port==end) return false;
        for (const char *p=port; p<end; ++p) {
            if (*p<'0'||*p>'9'||value>6553) return false;
            value=value*10+(*p-'0');
            if (value>65535) return false;
        }
        if (!value) return false;
        out->port=value;
    }
    size_t n=(size_t)(host_end-start); memcpy(out->host,start,n); out->host[n]=0;
    return true;
}
static inline bool https_origin_same(const char *a, const char *b)
{
    https_origin_t left,right;
    return https_origin_parse(a,&left)&&https_origin_parse(b,&right)&&
           left.port==right.port&&strcasecmp(left.host,right.host)==0;
}
#endif
