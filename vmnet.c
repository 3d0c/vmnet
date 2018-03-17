#include "go_vmnet.h"
#include "_cgo_export.h"

vmnet_interface * vmn_create(const char *id, void *goIface) {
	xpc_object_t interface_desc;
	__block interface_ref iface = NULL;
	__block vmnet_return_t iface_status = 0;
	__block unsigned int mtu = 0;
	__block unsigned int max_packet_size = 0;
	
	dispatch_queue_t if_create_q = dispatch_queue_create("org.np.vmnet.create",DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t iface_created = dispatch_semaphore_create(0);
	
	interface_desc = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_uint64(interface_desc, vmnet_operation_mode_key,
		VMNET_SHARED_MODE);

	uuid_t uuid;
	if (id != NULL && strlen(id) > 0) {
		if (uuid_parse(id, uuid) != 0) {
			uuid_generate_random(uuid);
		}
	} else {
		uuid_generate_random(uuid);
	}

	xpc_dictionary_set_uuid(interface_desc, vmnet_interface_id_key, uuid);
	
	vmnet_interface *result;
	result = malloc(sizeof(vmnet_interface));
	result->iref = NULL;

	__block void * bGoIface = goIface;


	iface = vmnet_start_interface(interface_desc, if_create_q, 
		^(vmnet_return_t status, xpc_object_t interface_param) { 
			iface_status = status;
			if (status != VMNET_SUCCESS || !interface_param) {
				dispatch_semaphore_signal(iface_created);
				return;
			}

			if (sscanf(xpc_dictionary_get_string(interface_param, vmnet_mac_address_key), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &result->mac[0], &result->mac[1], &result->mac[2], &result->mac[3], &result->mac[4], &result->mac[5]) != 6) {
				fprintf(stderr, "unexpected mac address\n");
				return;
			}

			result->mtu = xpc_dictionary_get_uint64(interface_param, vmnet_mtu_key);
			result->max_packet_size = xpc_dictionary_get_uint64(interface_param, vmnet_max_packet_size_key);
			
			dispatch_semaphore_signal(iface_created);
    	});

	dispatch_semaphore_wait(iface_created, DISPATCH_TIME_FOREVER);
	dispatch_release(if_create_q);

	if (iface == NULL || iface_status != VMNET_SUCCESS) {
		fprintf(stderr, "virtio_net: Could not create vmnet interface, "
			"permission denied or no entitlement?\n");
		free(result);
		return NULL;
	}

	result->iref = iface;

	dispatch_queue_t if_q = dispatch_queue_create("org.np.vmnet.events", 0);

	vmnet_interface_set_event_callback(iface, VMNET_INTERFACE_PACKETS_AVAILABLE,
		if_q, ^(interface_event_t event_id, xpc_object_t event) 
		{
			unsigned int navail = xpc_dictionary_get_uint64(event, vmnet_estimated_packets_available_key);
			emitEvent(bGoIface, event_id, navail);
		}
	);

	result->if_q = if_q;

	return result;
}

int vmn_destroy(vmnet_interface *vmnif) {
	__block interface_ref iface = vmnif->iref;

	vmnet_interface_set_event_callback(iface, VMNET_INTERFACE_PACKETS_AVAILABLE, NULL, NULL);

	dispatch_queue_t queue = dispatch_queue_create("org.np.vmnet.stop", DISPATCH_QUEUE_SERIAL);
	dispatch_semaphore_t sema = dispatch_semaphore_create(0);

	vmnet_return_t status = vmnet_stop_interface(iface, queue, ^(vmnet_return_t status) {
		dispatch_semaphore_signal(sema);
    });

	if (status == VMNET_SUCCESS) {
		dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
	}

	dispatch_release(queue);
	dispatch_release(iface->if_q);

	return(status);
}


vmnet_msg * vmn_read(vmnet_interface *vmnif) {
	interface_ref iface = vmnif->iref;

	vmnet_msg *msg = malloc(sizeof(struct vmnet_msg));
	msg->buf = malloc(2048);

	struct iovec iov;
	iov.iov_base = msg->buf;
	iov.iov_len = 2048;

	struct vmpktdesc v;
	v.vm_pkt_size = iov.iov_len;
	v.vm_pkt_iov = &iov;
	v.vm_pkt_iovcnt = 1;
	v.vm_flags = 0;

	int pktcnt = 1;

	vmnet_return_t status = vmnet_read(iface, &v, &pktcnt);
	msg->status = status;

	if (status == VMNET_SUCCESS && pktcnt <= 0) {
		msg->status = 2000;
	}

	if (status == VMNET_SUCCESS && pktcnt > 0) {
		msg->pkt_size = v.vm_pkt_size;
		msg->pkt_flags = v.vm_flags;
	}

	return msg;
}

vmnet_msg * vmn_write(vmnet_interface *vmnif, uint8_t *data, size_t len) {
	interface_ref iface = vmnif->iref;

	vmnet_msg *msg = malloc(sizeof(struct vmnet_msg));
	
	struct iovec iov;
	iov.iov_base = data;
	iov.iov_len = len;

	struct vmpktdesc v;
	v.vm_pkt_size = iov.iov_len;
	v.vm_pkt_iov = &iov;
	v.vm_pkt_iovcnt = 1;
	v.vm_flags = 0;

	int pktcnt = 1;

	vmnet_return_t status = vmnet_read(iface, &v, &pktcnt);
	msg->status = status;

	if (status == VMNET_SUCCESS && pktcnt <= 0) {
		msg->status = 2000;
	}

	if (status == VMNET_SUCCESS && pktcnt > 0) {
		msg->pkt_size = v.vm_pkt_size;
		msg->pkt_flags = v.vm_flags;
	}

	if (status == VMNET_SUCCESS && pktcnt <= 0) {
		msg->status = 2001;
	}

	return msg;	
}
