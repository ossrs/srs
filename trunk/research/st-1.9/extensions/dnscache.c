#include "stx.h"
#include "common.h"


/*****************************************
 * Basic types definitions
 */

typedef struct _stx_dns_data {
    struct in_addr *addrs;
    int num_addrs;
    int cur;
    time_t expires;
} stx_dns_data_t;


#define MAX_HOST_ADDRS 1024

static struct in_addr addr_list[MAX_HOST_ADDRS];

stx_cache_t *_stx_dns_cache = NULL;

extern int _stx_dns_ttl;
extern int _stx_dns_getaddrlist(const char *hostname, struct in_addr *addrs,
				int *num_addrs, st_utime_t timeout);


static unsigned long hash_hostname(const void *key)
{
    const char *name = (const char *)key;
    unsigned long hash = 0;

    while (*name)
	hash = (hash << 4) - hash + *name++; /* hash = hash * 15 + *name++ */

    return hash;
}

static void cleanup_entry(void *key, void *data)
{
    if (key)
	free(key);

    if (data) {
	if (((stx_dns_data_t *)data)->addrs)
	    free(((stx_dns_data_t *)data)->addrs);
	free(data);
    }
}

static int lookup_entry(const char *host, struct in_addr *addrs,
			int *num_addrs, int rotate)
{
    stx_cache_entry_t *entry;
    stx_dns_data_t *data;
    int n;

    entry = stx_cache_entry_lookup(_stx_dns_cache, host);
    if (entry) {
	data = (stx_dns_data_t *)stx_cache_entry_getdata(entry);
	if (st_time() <= data->expires) {
	    if (*num_addrs == 1) {
		if (rotate) {
		    *addrs = data->addrs[data->cur++];
		    if (data->cur >= data->num_addrs)
			data->cur = 0;
		} else {
		    *addrs = data->addrs[0];
		}
	    } else {
		n = STX_MIN(*num_addrs, data->num_addrs);
		memcpy(addrs, data->addrs, n * sizeof(*addrs));
		*num_addrs = n;
	    }

	    stx_cache_entry_release(_stx_dns_cache, entry);
	    return 1;
	}

	/*
	 * Cache entry expired: decrement its refcount and purge it from cache.
	 */
	stx_cache_entry_release(_stx_dns_cache, entry);
	stx_cache_entry_delete(_stx_dns_cache, entry);
    }

    return 0;
}

static void insert_entry(const char *host, struct in_addr *addrs, int count)
{
    stx_cache_entry_t *entry;
    stx_dns_data_t *data;
    char *key;
    size_t n;

    if (_stx_dns_ttl > 0) {
	key = strdup(host);
	data = (stx_dns_data_t *)malloc(sizeof(stx_dns_data_t));
	n = count * sizeof(*addrs);
	if (data) {
	    data->addrs = (struct in_addr *)malloc(n);
	    if (data->addrs)
		memcpy(data->addrs, addrs, n);
	    data->num_addrs = count;
	    data->cur = 0;
	    data->expires = st_time() + _stx_dns_ttl;
	}
	entry = stx_cache_entry_create(key, data, strlen(host) + 1 +
				       sizeof(stx_dns_data_t) + n +
				       stx_cache_entry_sizeof());
	if (key && data && data->addrs && entry &&
	    stx_cache_entry_insert(_stx_dns_cache, entry) == 0) {
	    stx_cache_entry_release(_stx_dns_cache, entry);
	    return;
	}

	if (entry)
	    stx_cache_entry_delete(_stx_dns_cache, entry);
	else
	    cleanup_entry(key, data);
    }
}



int _stx_dns_cache_getaddrlist(const char *hostname, struct in_addr *addrs,
			       int *num_addrs, st_utime_t timeout,
			       int rotate)
{
    char host[128];
    int n, count;

    if (!_stx_dns_cache)
	return _stx_dns_getaddrlist(hostname, addrs, num_addrs, timeout);

    for (n = 0; n < sizeof(host) - 1 && hostname[n]; n++) {
	host[n] = tolower(hostname[n]);
    }
    host[n] = '\0';

    if (lookup_entry(host, addrs, num_addrs, rotate))
	return 0;

    count = MAX_HOST_ADDRS;
    if (_stx_dns_getaddrlist(host, addr_list, &count, timeout) < 0)
	return -1;
    n = STX_MIN(*num_addrs, count);
    memcpy(addrs, addr_list, n * sizeof(*addrs));
    *num_addrs = n;

    insert_entry(host, addr_list, count);
    return 0;
}


int stx_dns_cache_init(size_t max_size, size_t max_bytes, size_t hash_size)
{
    _stx_dns_cache = stx_cache_create(max_size, max_bytes, hash_size,
				  hash_hostname,
				  (long (*)(const void *, const void *))strcmp,
				  cleanup_entry);
    if (!_stx_dns_cache)
	return -1;

    return 0;
}

void stx_dns_cache_getinfo(stx_cache_info_t *info)
{
    if (_stx_dns_cache)
	stx_cache_getinfo(_stx_dns_cache, info);
    else
	memset(info, 0, sizeof(stx_cache_info_t));
}	

int stx_dns_getaddrlist(const char *hostname, struct in_addr *addrs,
			int *num_addrs, st_utime_t timeout)
{
    return _stx_dns_cache_getaddrlist(hostname, addrs, num_addrs, timeout, 0);
}

int stx_dns_getaddr(const char *hostname, struct in_addr *addr,
		    st_utime_t timeout)
{
    int n = 1;

    return _stx_dns_cache_getaddrlist(hostname, addr, &n, timeout, 1);
}

