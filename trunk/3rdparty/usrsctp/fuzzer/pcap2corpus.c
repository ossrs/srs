/*-
 * Copyright (c) 2017 Michael Tuexen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Compile: cc -Wall -Werror -pedantic pcap2corpus.c -lpcap -o pcap2corpus
 *
 * Usage: pcap2corpus infile outfile_prefix [expression]
 *        if no expression, a pcap filter, is provided, sctp is used.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <pcap/pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long nr_read = 0;
static unsigned long nr_decaps = 0;

#define PRE_PADDING 1

struct args {
	struct bpf_program bpf_prog;
	char *filename_prefix;
	int (*is_ipv4)(const void *);
	int (*is_ipv6)(const void *);
	int linktype;
	unsigned int offset;
};

/*
 * SCTP protocol - RFC4960.
 */
struct sctphdr {
	uint16_t src_port;	/* source port */
	uint16_t dest_port;	/* destination port */
	uint32_t v_tag;		/* verification tag of packet */
	uint32_t checksum;	/* CRC32C checksum */
	/* chunks follow... */
} __attribute__((packed));

static int
loopback_is_ipv4(const void *bytes)
{
	uint32_t family;

	family = *(const uint32_t *)bytes;
	return (family == 2);
}

static int
loopback_is_ipv6(const void *bytes)
{
	uint32_t family;

	family = *(const uint32_t *)bytes;
	return (family == 24 || family == 28 || family == 30);
}

static int
ethernet_is_ipv4(const void *bytes)
{
	const struct ether_header *ether_hdr;

	ether_hdr = (const struct ether_header *)bytes;
	return (ntohs(ether_hdr->ether_type) == ETHERTYPE_IP);
}

static int
ethernet_is_ipv6(const void *bytes)
{
	const struct ether_header *ether_hdr;

	ether_hdr = (const struct ether_header *)bytes;
	return (ntohs(ether_hdr->ether_type) == ETHERTYPE_IPV6);
}

static void
packet_handler(u_char *user, const struct pcap_pkthdr *pkthdr, const u_char *bytes_in)
{
	struct args *args;
	const u_char *bytes_out;
	FILE *file;
	char *filename;
	const struct ip *ip4_hdr_in;
	const struct ip6_hdr *ip6_hdr_in;
	size_t offset, length;
	int null = 0;

	args = (struct args *)(void *)user;
	bytes_out = NULL;
	if (pcap_offline_filter(&args->bpf_prog, pkthdr, bytes_in) == 0) {
		goto out;
	}
	if (pkthdr->caplen < args->offset) {
		goto out;
	}
	if (args->is_ipv4(bytes_in)) {
		offset = args->offset + sizeof(struct ip) + sizeof(struct sctphdr);
		if (pkthdr->caplen < offset) {
			goto out;
		}
		ip4_hdr_in = (const struct ip *)(const void *)(bytes_in + args->offset);
		if (ip4_hdr_in->ip_p == IPPROTO_SCTP) {
			unsigned int ip4_hdr_len;

			ip4_hdr_len = ip4_hdr_in->ip_hl << 2;
			offset = args->offset + ip4_hdr_len + sizeof(struct sctphdr);
			if (pkthdr->caplen < offset) {
				goto out;
			}
			bytes_out = bytes_in + offset;
			length = pkthdr->caplen - offset;
		}
	}
	if (args->is_ipv6(bytes_in)) {
		offset = args->offset + sizeof(struct ip6_hdr) + sizeof(struct sctphdr);
		if (pkthdr->caplen < offset) {
			goto out;
		}
		ip6_hdr_in = (const struct ip6_hdr *)(bytes_in + args->offset);
		if (ip6_hdr_in->ip6_nxt == IPPROTO_SCTP) {
			bytes_out = bytes_in + offset;
			length = pkthdr->caplen - offset;
		}
	}
out:
	nr_read++;
	if (bytes_out != NULL) {
		if (asprintf(&filename, "%s-%06lu", args->filename_prefix, nr_decaps) < 0) {
			return;
		}
		file = fopen(filename, "w");
		fwrite(&null, 1, PRE_PADDING, file);
		fwrite(bytes_out, length, 1, file);
		fclose(file);
		free(filename);
		nr_decaps++;
	}
}

static char *
get_filter(int argc, char *argv[])
{
	char *result, *c;
	size_t len;
	int i;

	if (argc == 3) {
		if (asprintf(&result, "%s", "sctp") < 0) {
			return (NULL);
		}
	} else {
		len = 0;
		for (i = 3; i < argc; i++) {
			len += strlen(argv[i]) + 1;
		}
		result = malloc(len);
		c = result;
		for (i = 3; i < argc; i++) {
			c = stpcpy(c, argv[i]);
			if (i < argc - 1) {
				*c++ = ' ';
			}
		}
	}
	return (result);
}

int
main(int argc, char *argv[])
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *pcap_reader;
	char *filter;
	struct args args;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s infile outfile_prefix [expression]\n", argv[0]);
		return (-1);
	}
	args.filename_prefix = argv[2];
	pcap_reader = pcap_open_offline(argv[1], errbuf);
	if (pcap_reader == NULL) {
		fprintf(stderr, "Can't open input file %s: %s\n", argv[1], errbuf);
		return (-1);
	}
	args.linktype = pcap_datalink(pcap_reader);
	switch (args.linktype) {
	case DLT_NULL:
		args.is_ipv4 = loopback_is_ipv4;
		args.is_ipv6 = loopback_is_ipv6;
		args.offset = sizeof(uint32_t);
		break;
	case DLT_EN10MB:
		args.is_ipv4 = ethernet_is_ipv4;
		args.is_ipv6 = ethernet_is_ipv6;
		args.offset = sizeof(struct ether_header);
		break;
	default:
		fprintf(stderr, "Datalink type %d not supported\n", args.linktype);
		pcap_close(pcap_reader);
		return (-1);
	}
	filter = get_filter(argc, argv);
	if (pcap_compile(pcap_reader, &args.bpf_prog, filter, 1, PCAP_NETMASK_UNKNOWN) < 0) {
		fprintf(stderr, "Can't compile filter %s: %s\n", filter, pcap_geterr(pcap_reader));
		free(filter);
		pcap_close(pcap_reader);
		return (-1);
	}
	free(filter);
	pcap_dispatch(pcap_reader, 0, packet_handler, (u_char *)&args);
	pcap_close(pcap_reader);
	fprintf(stderr, "%lu packets processed\n", nr_read);
	fprintf(stderr, "%lu packets decapsulated\n", nr_decaps);
	return (0);
}
