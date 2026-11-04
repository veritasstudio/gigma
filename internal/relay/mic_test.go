package relay

import (
	"encoding/binary"
	"net"
	"testing"
	"time"
)

func TestBrowserMicFansOutToBrowsersAndPlugins(t *testing.T) {
	const sid = "0123456789abcdef"
	h := New(func(string, string) bool { return false }, func(s, secret string) (string, bool) {
		return "peerHOST00000001", s == sid && secret == "hosthosthosthost"
	})
	addr := freeUDPAddr(t)
	go h.ListenUDP(addr)
	time.Sleep(100 * time.Millisecond)

	ra, _ := net.ResolveUDPAddr("udp", addr)
	host, err := net.DialUDP("udp", nil, ra)
	if err != nil {
		t.Fatal(err)
	}
	defer host.Close()
	host.SetReadDeadline(time.Now().Add(2 * time.Second))
	buf := make([]byte, 2048)
	host.Write(wrap(sid, "hosthosthosthost", zeroID, nil))
	if _, err := host.Read(buf); err != nil {
		t.Fatal("keepalive ack", err)
	}

	a, fa, ua := h.SubscribeTalker(sid, "Asha")
	defer ua()
	b, fb, ub := h.SubscribeTalker(sid, "Ben")
	defer ub()
	if a == b {
		t.Fatal("talker ids must differ")
	}

	in := []byte{MicCodecOpus, 0, 0, 0, 7, 0, 0, 0, 0xde, 0xad, 0xbe, 0xef}
	if !h.Talk(sid, a, in) {
		t.Fatal("talk rejected")
	}
	if h.Talk(sid, a, []byte{9, 0, 0, 0, 0, 0, 0, 0, 1}) {
		t.Fatal("bad codec accepted")
	}

	// the other browser gets it, the speaker doesn't
	select {
	case f := <-fb:
		if binary.LittleEndian.Uint32(f[0:]) != 7 || binary.LittleEndian.Uint32(f[4:]) != 0 ||
			f[8] != MicCodecOpus || binary.LittleEndian.Uint16(f[10:]) != a || string(f[12:]) != "\xde\xad\xbe\xef" {
			t.Fatalf("browser frame %x", f)
		}
	case <-time.After(time.Second):
		t.Fatal("no frame for other browser")
	}
	select {
	case f := <-fa:
		t.Fatalf("speaker got own frame %x", f)
	default:
	}

	// the relayed plugin gets GGB1
	n, err := host.Read(buf)
	if err != nil || string(buf[:4]) != "GGB1" || binary.LittleEndian.Uint16(buf[4:]) != a ||
		buf[6] != MicCodecOpus || binary.LittleEndian.Uint32(buf[8:]) != 7 || string(buf[12:n]) != "\xde\xad\xbe\xef" {
		t.Fatalf("plugin frame %x %v", buf[:n], err)
	}

	ts := h.Talkers(sid)
	if len(ts) != 2 || ts[0].Name != "Asha" || !ts[0].Live || ts[1].Live {
		t.Fatalf("talkers %+v", ts)
	}
}
