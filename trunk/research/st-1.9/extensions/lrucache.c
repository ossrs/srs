#include "stx.h"
#include "common.h"


/*****************************************
 * Basic types definitions
 */

struct _stx_centry {
  void               *key;           /* key for doing lookups */
  void               *data;          /* data in the cache */
  size_t             weight;         /* "weight" of this entry */
  struct _stx_centry *next;          /* next entry */
  struct _stx_centry **pthis;
  stx_clist_t        lru_link;       /* for putting this entry on LRU list */
  int                ref_count;      /* use count for this entry */
  int                delete_pending; /* pending delete flag */
};

struct _stx_cache {
  size_t	    max_size;    /* max size of cache */
  size_t            cur_size;    /* current size of cache */

  size_t            max_weight;  /* cache capacity */
  size_t            cur_weight;  /* current total "weight" of all entries */

  size_t	    hash_size;   /* size of hash table */
  stx_cache_entry_t **table;     /* hash table for this cache */

  stx_clist_t       lru_list;    /* least-recently-used list */

  /* Cache stats */
  unsigned long     hits;        /* num cache hits */
  unsigned long     lookups;     /* num cache lookups */
  unsigned long     inserts;     /* num inserts */
  unsigned long     deletes;     /* num deletes */

  /* Functions */
  unsigned long (*key_hash_fn)(const void *);
  long          (*key_cmp_fn)(const void *, const void *);
  void          (*cleanup_fn)(void *, void *);
};


#define STX_CACHE_ENTRY_PTR(_qp) \
  ((stx_cache_entry_t *)((char *)(_qp) - offsetof(stx_cache_entry_t, lru_link)))


/*****************************************
 * Cache methods
 */

stx_cache_t *stx_cache_create(size_t max_size, size_t max_weight,
			      size_t hash_size,
			      unsigned long (*key_hash_fn)(const void *key),
			      long (*key_cmp_fn)(const void *key1,
						 const void *key2),
			      void (*cleanup_fn)(void *key, void *data))
{
    stx_cache_t *newcache;

    newcache = (stx_cache_t *)calloc(1, sizeof(stx_cache_t));
    if (newcache == NULL)
	return NULL;
    newcache->table = (stx_cache_entry_t **)calloc(hash_size,
					         sizeof(stx_cache_entry_t *));
    if (newcache->table == NULL) {
	free(newcache);
	return NULL;
    }

    newcache->max_size = max_size;
    newcache->max_weight = max_weight;
    newcache->hash_size = hash_size;
    STX_CLIST_INIT_CLIST(&(newcache->lru_list));
    newcache->key_hash_fn = key_hash_fn;
    newcache->key_cmp_fn = key_cmp_fn;
    newcache->cleanup_fn = cleanup_fn;

    return newcache;
}


void stx_cache_empty(stx_cache_t *cache)
{
    size_t i;
    stx_cache_entry_t *entry, *next_entry;

    for (i = 0; i < cache->hash_size; i++) {
	entry = cache->table[i];
	while (entry) {
	    next_entry = entry->next;
	    stx_cache_entry_delete(cache, entry);
	    entry = next_entry;
	}
    }
}


void stx_cache_traverse(stx_cache_t *cache,
			void (*callback)(void *key, void *data))
{
    size_t i;
    stx_cache_entry_t *entry;

    for (i = 0; i < cache->hash_size; i++) {
	for (entry = cache->table[i]; entry; entry = entry->next) {
	    if (!entry->delete_pending)
		(*callback)(entry->key, entry->data);
	}
    }
}


void stx_cache_traverse_lru(stx_cache_t *cache,
			    void (*callback)(void *key, void *data),
			    unsigned int n)
{
    stx_clist_t *q;
    stx_cache_entry_t *entry;

    for (q = STX_CLIST_HEAD(&cache->lru_list); q != &cache->lru_list && n;
	 q = q->next, n--) {
	entry = STX_CACHE_ENTRY_PTR(q);
	(*callback)(entry->key, entry->data);
    }
}


void stx_cache_traverse_mru(stx_cache_t *cache,
			    void (*callback)(void *key, void *data),
			    unsigned int n)
{
    stx_clist_t *q;
    stx_cache_entry_t *entry;

    for (q = STX_CLIST_TAIL(&cache->lru_list); q != &cache->lru_list && n;
	 q = q->prev, n--) {
	entry = STX_CACHE_ENTRY_PTR(q);
	(*callback)(entry->key, entry->data);
    }
}


size_t stx_cache_getsize(stx_cache_t *cache)
{
    return cache->cur_size;
}


size_t stx_cache_getweight(stx_cache_t *cache)
{
    return cache->cur_weight;
}


void stx_cache_getinfo(stx_cache_t *cache, stx_cache_info_t *info)
{
    info->max_size = cache->max_size;
    info->max_weight = cache->max_weight;
    info->hash_size = cache->hash_size;
    info->cur_size = cache->cur_size;
    info->cur_weight = cache->cur_weight;
    info->hits = cache->hits;
    info->lookups = cache->lookups;
    info->inserts = cache->inserts;
    info->deletes = cache->deletes;
}


/*****************************************
 * Cache entry methods
 */

stx_cache_entry_t *stx_cache_entry_create(void *key, void *data,
					  size_t weight)
{
    stx_cache_entry_t *newentry;

    newentry = (stx_cache_entry_t *)calloc(1, sizeof(stx_cache_entry_t));
    if (newentry == NULL)
	return NULL;

    newentry->key = key;
    newentry->data = data;
    newentry->weight = weight;

    return newentry;
}


void stx_cache_entry_delete(stx_cache_t *cache, stx_cache_entry_t *entry)
{
    entry->delete_pending = 1;

    if (entry->ref_count > 0)
	return;

    if (entry->pthis) {
	*entry->pthis = entry->next;
	if (entry->next)
	    entry->next->pthis = entry->pthis;

	cache->cur_size--;
	cache->cur_weight -= entry->weight;
	cache->deletes++;
	STX_CLIST_REMOVE_LINK(&(entry->lru_link));
    }

    if (cache->cleanup_fn)
	cache->cleanup_fn(entry->key, entry->data);

    entry->pthis = NULL;
    entry->key   = NULL;
    entry->data  = NULL;
    free(entry);
}


stx_cache_entry_t *stx_cache_entry_lookup(stx_cache_t *cache, const void *key)
{
    unsigned long bucket;
    stx_cache_entry_t *entry;

    cache->lookups++;
    bucket = cache->key_hash_fn(key) % cache->hash_size;
    for (entry = cache->table[bucket]; entry; entry = entry->next) {
	if (!entry->delete_pending && cache->key_cmp_fn(key, entry->key) == 0)
	    break;
    }
    if (entry) {
	cache->hits++;
	if (entry->ref_count == 0)
	    STX_CLIST_REMOVE_LINK(&(entry->lru_link));
	entry->ref_count++;
    }

    return entry;
}


void stx_cache_entry_release(stx_cache_t *cache, stx_cache_entry_t *entry)
{
    if (entry->ref_count == 0)
	return;

    entry->ref_count--;

    if (entry->ref_count == 0) {
	STX_CLIST_APPEND_LINK(&(entry->lru_link), &(cache->lru_list));
	if (entry->delete_pending)
	    stx_cache_entry_delete(cache, entry);
    }
}


int stx_cache_entry_insert(stx_cache_t *cache, stx_cache_entry_t *entry)
{
    stx_cache_entry_t *old_entry;
    unsigned long bucket;

    /*
     * If cache capacity is exceeded, try to remove LRU entries till there is
     * enough room or LRU list is empty.
     */
    while (cache->cur_weight + entry->weight > cache->max_weight) {
	old_entry = stx_cache_entry_getlru(cache);
	if (!old_entry) {
	    /* cache capacity is exceeded and all entries are in use */
	    return -1;
	}
	stx_cache_entry_delete(cache, old_entry);
    }

    /* If cache size is exceeded, remove LRU entry */
    if (cache->cur_size >= cache->max_size) {
	old_entry = stx_cache_entry_getlru(cache);
	if (!old_entry) {
	    /* cache size is exceeded and all entries are in use */
	    return -1;
	}
	stx_cache_entry_delete(cache, old_entry);
    }

    /* Don't add duplicate entries in the cache */
    bucket = cache->key_hash_fn(entry->key) % cache->hash_size;
    for (old_entry = cache->table[bucket]; old_entry;
	 old_entry = old_entry->next) {
	if (!old_entry->delete_pending &&
	    cache->key_cmp_fn(entry->key, old_entry->key) == 0)
	    break;
    }
    if (old_entry)
	stx_cache_entry_delete(cache, old_entry);

    /* Insert in the hash table */
    entry->next = cache->table[bucket];
    cache->table[bucket] = entry;
    entry->pthis = &cache->table[bucket];
    if (entry->next)
	entry->next->pthis = &entry->next;
    entry->ref_count++;

    cache->inserts++;
    cache->cur_size++;
    cache->cur_weight += entry->weight;

    return 0;
}


stx_cache_entry_t *stx_cache_entry_getlru(stx_cache_t *cache)
{
    if (STX_CLIST_IS_EMPTY(&(cache->lru_list)))
	return NULL;

    return STX_CACHE_ENTRY_PTR(STX_CLIST_HEAD(&(cache->lru_list)));
}


int stx_cache_entry_sizeof(void)
{
    return (int)sizeof(stx_cache_entry_t);
}


void *stx_cache_entry_getdata(stx_cache_entry_t *entry)
{
    return entry->data;
}


void *stx_cache_entry_getkey(stx_cache_entry_t *entry)
{
    return entry->key;
}


size_t stx_cache_entry_getweight(stx_cache_entry_t *entry)
{
    return entry->weight;
}

