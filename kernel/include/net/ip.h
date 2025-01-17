/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef NET_IP_H
#define NET_IP_H

// includes
#include <stdint.h>

#define IP4_PROTOCAL_ICMP 1
#define IP4_PROTOCAL_TCP 6
#define IP4_PROTOCAL_UDP 17

struct socket;
struct sk_buff;

struct __attribute__((packed)) ip4_pseudo_header
{
	uint32_t source_ip;
	uint32_t dest_ip;
	uint8_t zeros;
	uint8_t protocal;
	uint16_t transport_length;
};

struct __attribute__((packed)) ip4_packet
{
	uint8_t ihl : 4;
	uint8_t version : 4;
	uint8_t type_of_service;
	uint16_t total_length;
	uint16_t identification;
	uint16_t fragment_offset : 13;
	uint8_t flags : 3;
	uint8_t time_to_live;
	uint8_t protocal;
	uint16_t header_checksum;
	uint32_t source_ip;
	uint32_t dest_ip;
	uint8_t payload[];
};

struct ip4_packet *ip4_build_header(struct ip4_packet *packet, uint16_t packet_size, uint8_t protocal, uint32_t source_ip, uint32_t dest_ip, uint32_t identification);
void ip4_sendmsg(struct socket *sock, struct sk_buff *skb);
int ip4_rcv(struct sk_buff *skb);
int ip4_validate_header(struct ip4_packet *ip, uint8_t protocal);

#endif