package vmnet

import (
	"fmt"
	"testing"

	"github.com/google/gopacket"
	"github.com/google/gopacket/layers"
)

func TestNewInterface(t *testing.T) {
	vmi := NewInterface("11111111-6535-6332-2D64-6130662D3636")
	if vmi.vmnetInterface == nil {
		t.Fatal("iref is nil")
	}

	for {
		buf, sz, err := vmi.ReadPacket()
		if err != nil {
			t.Fatal(err)
		}

		if sz == 0 {
			fmt.Println("sz:", sz)
			continue
		}

		p := gopacket.NewPacket(buf, layers.LayerTypeEthernet, gopacket.NoCopy)
		fmt.Printf("read: %s\n", p)
	}
}
