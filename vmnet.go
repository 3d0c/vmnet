package vmnet

/*
#cgo CFLAGS: -framework vmnet
#cgo LDFLAGS: -framework vmnet

#include "go_vmnet.h"
*/
import "C"

import (
	"errors"
	"io"
	"log"
	"net"
	"sync"
	"unsafe"
)

var (
	ErrGenericFailure                      = errors.New("vmnet: generic failure")
	ErrOutOfMemory                         = errors.New("vmnet: out of memory")
	ErrInvalidArgument                     = errors.New("vmnet: invalid argument")
	ErrInterfaceSetupIsNotComplete         = errors.New("vmnet: interface setup is not complete")
	ErrPermissionDenied                    = errors.New("vmnet: permission denied")
	ErrPacketSizeLargerThanMTU             = errors.New("vmnet: packet size larger than MTU")
	ErrBuffersExhaustedTemporarilyInKernel = errors.New("vmnet: buffers exhausted temporarily in kernel")
	ErrPacketsLargerThanLimit              = errors.New("vmnet: packets larger than limit")
	errNoMorePackets                       = errors.New("vmnet: no more packets")
	errNotWritten                          = errors.New("vmnet: packet not written")
)

var errCodesMap = map[int]error{
	1001: ErrGenericFailure,
	1002: ErrOutOfMemory,
	1003: ErrInvalidArgument,
	1004: ErrInterfaceSetupIsNotComplete,
	1005: ErrPermissionDenied,
	1006: ErrPacketSizeLargerThanMTU,
	1007: ErrBuffersExhaustedTemporarilyInKernel,
	1008: ErrPacketsLargerThanLimit,
	2000: errNoMorePackets,
	2001: errNotWritten,
}

func errCodeToErr(code int) error {
	err := errCodesMap[code]
	if err == nil {
		err = ErrGenericFailure
	}

	return err
}

type EventType uint32

const (
	packetsAvailableEvent EventType = 1 << 0
)

type Msg struct {
	status   int
	buf      []byte
	pktSize  int
	pktFlags uint32
}

type Interface struct {
	events        chan Event
	readC         chan packet
	closed        bool
	id            string
	macStr        string
	mac           net.HardwareAddr
	mtu           uint64
	maxPacketSize uint64

	iref   unsafe.Pointer
	eventQ unsafe.Pointer

	vmnetInterface *C.struct_vmnet_interface

	*sync.RWMutex
}

type Event struct {
	Type EventType
}

type packet struct {
	buf   []byte
	flags uint32
	err   error
}

func NewInterface(id string) Interface {
	vmi := Interface{}

	cId := C.CString(id)
	defer C.free(unsafe.Pointer(cId))

	vmi.vmnetInterface = C.vmn_create(cId, unsafe.Pointer(&vmi))

	vmi.readC = make(chan packet)
	vmi.events = make(chan Event)
	vmi.maxPacketSize = uint64(vmi.vmnetInterface.max_packet_size)

	return vmi
}

func (iface Interface) Destroy() error {
	status := C.vmn_destroy(unsafe.Pointer(iface.vmnetInterface))
	if int(status) != 1000 {
		return errCodeToErr(int(status))
	}

	iface.RLock()
	defer iface.RUnlock()

	close(iface.readC)
	close(iface.events)

	C.free(unsafe.Pointer(iface.vmnetInterface))

	return nil
}

//export emitEvent
func emitEvent(ptr unsafe.Pointer, eventType uint32, nPktAvail uint64) {
	iface := (*Interface)(ptr)
	etype := EventType(eventType)
	log.Printf("emitEvent, %v, %v, %v\n", ptr, eventType, nPktAvail)
	switch etype {
	case packetsAvailableEvent:
		for i := 0; i < int(nPktAvail); i++ {
			pkt, err := iface.readPacket()
			if err == errNoMorePackets {
				break
			}
			log.Println("write to chan")
			iface.readC <- pkt
			if err != nil {
				break
			}
		}
	default:
		iface.events <- Event{Type: etype}
	}
}

func (iface Interface) readPacket() (packet, error) {
	var pkt packet

	cMsg := C.vmn_read(unsafe.Pointer(iface.vmnetInterface))

	if int(cMsg.status) != 1000 {
		err := errCodeToErr(int(cMsg.status))

		pkt.err = err
		return pkt, err
	}

	pkt.buf = C.GoBytes(unsafe.Pointer(cMsg.buf), C.int(cMsg.pkt_size))
	pkt.flags = uint32(cMsg.pkt_flags)

	C.free(unsafe.Pointer(cMsg.buf))
	C.free(unsafe.Pointer(cMsg))

	return pkt, nil
}

func (iface Interface) ReadPacket() ([]byte, int, error) {
	pkt, open := <-iface.readC
	if !open {
		return []byte{}, 0, io.EOF
	}
	if pkt.err != nil {
		return []byte{}, 0, pkt.err
	}

	return pkt.buf, len(pkt.buf), nil
}

func (iface Interface) WritePacket(p []byte) (n int, err error) {
	if uint64(len(p)) > iface.maxPacketSize {
		log.Printf("len(p) > maxPacketsize, %d > %d\n", len(p), iface.maxPacketSize)
		return 0, io.ErrShortWrite
	}

	iface.RLock()
	defer iface.RUnlock()

	if iface.closed {
		log.Printf("closed\n")
		return 0, io.EOF
	}

	cMsg := C.vmn_write(unsafe.Pointer(iface.vmnetInterface), (*C.uint8_t)(unsafe.Pointer(&p[0])), C.size_t(len(p)))

	if int(cMsg.status) != 1000 {
		return 0, errCodeToErr(int(cMsg.status))
	}

	return int(cMsg.pkt_size), nil
}
