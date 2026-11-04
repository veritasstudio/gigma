package relay

import (
	"bytes"
	"net"
	"testing"
	"time"
)

func freeUDPAddr(t *testing.T) string {
	c, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	a := c.LocalAddr().String()
	c.Close()
	return a
}

func wrap(sid, secret, dst string, payload []byte) []byte {
	b := append([]byte("GGR1"), sid...)
	b = append(b, secret...)
	b = append(b, dst...)
	return append(b, payload...)
}

func TestPeerRelayForwardsBothWays(t *testing.T) {
	const sid = "0123456789abcdef"
	secrets := map[string]string{"hosthosthosthost": "peerHOST00000001", "guestsecret00001": "peerGUEST0000001"}
	h := New(func(string, string) bool { return false }, func(s, secret string) (string, bool) {
		id, ok := secrets[secret]
		return id, ok && s == sid
	})
	addr := freeUDPAddr(t)
	go h.ListenUDP(addr)
	time.Sleep(100 * time.Millisecond)

	dial := func() *net.UDPConn {
		ra, _ := net.ResolveUDPAddr("udp", addr)
		c, err := net.DialUDP("udp", nil, ra)
		if err != nil {
			t.Fatal(err)
		}
		c.SetReadDeadline(time.Now().Add(2 * time.Second))
		return c
	}
	a, b := dial(), dial()
	defer a.Close()
	defer b.Close()
	buf := make([]byte, 2048)

	// keepalives register both and are acked
	for _, x := range []struct {
		c      *net.UDPConn
		secret string
	}{{a, "hosthosthosthost"}, {b, "guestsecret00001"}} {
		x.c.Write(wrap(sid, x.secret, zeroID, nil))
		n, err := x.c.Read(buf)
		if err != nil || n != 20 || string(buf[4:20]) != zeroID {
			t.Fatalf("keepalive ack: n=%d err=%v", n, err)
		}
	}

	a.Write(wrap(sid, "hosthosthosthost", "peerGUEST0000001", []byte("/aoo/sink/1/data")))
	n, err := b.Read(buf)
	if err != nil || string(buf[4:20]) != "peerHOST00000001" || !bytes.Equal(buf[20:n], []byte("/aoo/sink/1/data")) {
		t.Fatalf("host→guest: %q %v", buf[:n], err)
	}
	b.Write(wrap(sid, "guestsecret00001", "peerHOST00000001", []byte("/aoo/src/1/ping")))
	n, err = a.Read(buf)
	if err != nil || string(buf[4:20]) != "peerGUEST0000001" || !bytes.Equal(buf[20:n], []byte("/aoo/src/1/ping")) {
		t.Fatalf("guest→host: %q %v", buf[:n], err)
	}

	// wrong secret is dropped (no forward)
	a.Write(wrap(sid, "badbadbadbadbadb", "peerGUEST0000001", []byte("x")))
	b.SetReadDeadline(time.Now().Add(300 * time.Millisecond))
	if _, err := b.Read(buf); err == nil {
		t.Fatal("forwarded packet with bad secret")
	}
	if got := h.RelayedPeers(sid); !got["peerHOST00000001"] || !got["peerGUEST0000001"] {
		t.Fatalf("relayed peers: %v", got)
	}
	if s := h.PeerStats(); s.Forwarded != 2 || s.Dropped != 1 {
		t.Fatalf("stats %+v", s)
	}
}
