#ifndef __go_vmnet_h__
#define __go_vmnet_h__

#include <stdio.h>
#include <dispatch/dispatch.h>
#include <sys/uio.h>
#include <vmnet/vmnet.h>

typedef struct vmnet_msg {
	int status;
	uint8_t *buf;
	size_t pkt_size; 
	uint32_t pkt_flags;
} vmnet_msg;

typedef struct vmnet_interface {
	interface_ref iref;
	uint64_t mtu;
	uint64_t max_packet_size;
	uint8_t mac[6];
	uint64_t status;
	dispatch_queue_t if_q;
} vmnet_interface;

vmnet_interface * vmn_create(const char *id, void *goIface);
vmnet_msg * vmn_read(vmnet_interface *vmnif);
vmnet_msg * vmn_write(vmnet_interface *vmnif, uint8_t *data, size_t len);
int vmn_destroy(vmnet_interface *vmnif);

#endif
