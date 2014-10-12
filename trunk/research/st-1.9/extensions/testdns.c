#include "stx.h"
#include <netinet/in.h>
#include <arpa/inet.h>


#define MAX_ADDRS 128
#define TIMEOUT (4*1000000LL)

static void do_resolve(const char *host)
{
    struct in_addr addrs[MAX_ADDRS];
    int i, n = MAX_ADDRS;

    if (stx_dns_getaddrlist(host, addrs, &n, TIMEOUT) < 0) {
	fprintf(stderr, "stx_dns_getaddrlist: can't resolve %s: ", host);
	if (h_errno == NETDB_INTERNAL)
	    perror("");
	else
	    herror("");
    } else {
	if (n > 0)
	    printf("%-40s %s\n", (char *)host, inet_ntoa(addrs[0]));
	for (i = 1; i < n; i++)
	    printf("%-40s %s\n", "", inet_ntoa(addrs[i]));
    }
}

static void show_info(void)
{
    stx_cache_info_t info;

    stx_dns_cache_getinfo(&info);
    printf("DNS cache info:\n\n");
    printf("max_size:  %8d\n", (int)info.max_size);
    printf("capacity:  %8d bytes\n", (int)info.max_weight);
    printf("hash_size: %8d\n", (int)info.hash_size);
    printf("cur_size:  %8d\n"
	   "cur_mem:   %8d bytes\n"
	   "hits:      %8d\n"
	   "lookups:   %8d\n"
	   "inserts:   %8d\n"
	   "deletes:   %8d\n",
	   (int)info.cur_size, (int)info.cur_weight, (int)info.hits,
	   (int)info.lookups, (int)info.inserts, (int)info.deletes);
}

extern stx_cache_t *_stx_dns_cache;

static void printhost(void *host, void *data)
{
    printf("%s\n", (char *)host);
}

static void show_lru(void)
{
    printf("LRU hosts:\n\n");
    stx_cache_traverse_lru(_stx_dns_cache, printhost, 10);
}

static void show_mru(void)
{
    printf("MRU hosts:\n\n");
    stx_cache_traverse_mru(_stx_dns_cache, printhost, 10);
}

static void flush_cache(void)
{
    stx_cache_empty(_stx_dns_cache);
    printf("DNS cache is empty\n");
}


int main()
{
    char line[256];
    char str[sizeof(line)];

    st_init();
    stx_dns_cache_init(100, 10000, 101);

    for ( ; ; ) {
	fputs("> ", stdout);
	fflush(stdout);
	if (!fgets(line, sizeof(line), stdin))
	    break;
	if (sscanf(line, "%s", str) != 1)
	    continue;
	if (strcmp(str, "exit") == 0 || strcmp(str, "quit") == 0)
	    break;
	if (strcmp(str, "info") == 0) {
	    show_info();
	    continue;
	}
	if (strcmp(str, "lru") == 0) {
	    show_lru();
	    continue;
	}
	if (strcmp(str, "mru") == 0) {
	    show_mru();
	    continue;
	}
	if (strcmp(str, "flush") == 0) {
	    flush_cache();
	    continue;
	}

	do_resolve(str);
    }

    return 0;
}

