#ifndef _STX_H_
#define _STX_H_

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include <errno.h>
#include "st.h"


#ifdef __cplusplus
extern "C" {
#endif


/*****************************************
 * Basic types definitions
 */

typedef struct _stx_centry stx_cache_entry_t;
typedef struct _stx_cache  stx_cache_t;

/* This is public type */
typedef struct _stx_cache_info {
  size_t max_size;
  size_t max_weight;
  size_t hash_size;
  size_t cur_size;
  size_t cur_weight;
  unsigned long hits;
  unsigned long lookups;
  unsigned long inserts;
  unsigned long deletes;
} stx_cache_info_t;


/*****************************************
 * Cache and cache entry methods
 */

stx_cache_t *stx_cache_create(size_t max_size, size_t max_weight,
			      size_t hash_size,
			      unsigned long (*key_hash_fn)(const void *key),
			      long (*key_cmp_fn)(const void *key1,
						 const void *key2),
			      void (*cleanup_fn)(void *key, void *data));
void stx_cache_empty(stx_cache_t *cache);
void stx_cache_traverse(stx_cache_t *cache,
			void (*callback)(void *key, void *data));
void stx_cache_traverse_lru(stx_cache_t *, void (*)(void *, void *),
			    unsigned int);
void stx_cache_traverse_mru(stx_cache_t *, void (*)(void *, void *),
			    unsigned int);
void stx_cache_getinfo(stx_cache_t *cache, stx_cache_info_t *info);
size_t stx_cache_getsize(stx_cache_t *cache);
size_t stx_cache_getweight(stx_cache_t *cache);


stx_cache_entry_t *stx_cache_entry_create(void *key, void *data,
					  size_t weight);
void stx_cache_entry_delete(stx_cache_t *cache, stx_cache_entry_t *entry);
stx_cache_entry_t *stx_cache_entry_lookup(stx_cache_t *cache, const void *key);
void stx_cache_entry_release(stx_cache_t *, stx_cache_entry_t *);
int stx_cache_entry_insert(stx_cache_t *cache, stx_cache_entry_t *entry);
stx_cache_entry_t *stx_cache_entry_getlru(stx_cache_t *cache);
int stx_cache_entry_sizeof(void);
void *stx_cache_entry_getdata(stx_cache_entry_t *entry);
void *stx_cache_entry_getkey(stx_cache_entry_t *entry);
size_t stx_cache_entry_getweight(stx_cache_entry_t *entry);


int stx_dns_cache_init(size_t max_size, size_t max_bytes, size_t hash_size);
void stx_dns_cache_getinfo(stx_cache_info_t *info);
int stx_dns_getaddrlist(const char *hostname, struct in_addr *addrs,
			int *num_addrs, st_utime_t timeout);
int stx_dns_getaddr(const char *hostname, struct in_addr *addr,
		    st_utime_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* !_STX_H_ */

