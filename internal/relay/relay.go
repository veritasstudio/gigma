// Package relay bridges host-plugin audio to browser guests.
//
// Browsers cannot speak AOO (raw UDP/OSC), so the host plugin additionally
// sends a plain PCM16 stream to this UDP port on the VPS (outbound from the
// producer, so no NAT/port-forwarding on their side). The broker fans packets
// out per session to WebSocket listeners at /api/sessions/{id}/listen.
//
// UDP packet (little endian):
//
//	0  "GGM1"
//	4  sessionId (16 ascii hex)
//	20 hostId    (16 ascii hex)
//	36 seq u32 | 40 sampleRate u32 | 44 channels u8 | 45..47 reserved
//	48 int16 interleaved samples
//
// WebSocket binary frame sent to browsers: bytes [36:] of the UDP packet
// (seq, sampleRate, channels, reserved, samples) — the host token is stripped.
//
// The same UDP port also relays plugin↔plugin AOO traffic ("GGR1", see peer.go).
package relay

import (
	"bytes"
	"log"
	"net"
	"sync"
	"time"
)

const headerLen = 48

var magic = []byte("GGM1")

// HostAuth reports whether hostID currently owns sessionID.
type HostAuth func(sessionID, hostID string) bool

type Hub struct {
	mu    sync.RWMutex
	subs  map[string]map[chan []byte]struct{}
	auth  HostAuth
	peers *peerTable
}

// New builds a hub. peerAuth may be nil to disable the plugin↔plugin relay.
func New(auth HostAuth, peerAuth PeerAuth) *Hub {
	h := &Hub{subs: map[string]map[chan []byte]struct{}{}, auth: auth}
	if peerAuth != nil {
		h.peers = newPeerTable(peerAuth)
	}
	return h
}

// RelayedPeers returns the peer IDs of sessionID currently registered on the
// AOO relay (nil if the peer relay is disabled).
func (h *Hub) RelayedPeers(sessionID string) map[string]bool {
	if h.peers == nil {
		return nil
	}
	return h.peers.relayed(sessionID, time.Now())
}

// PeerStats returns plugin↔plugin relay counters.
func (h *Hub) PeerStats() PeerStats {
	if h.peers == nil {
		return PeerStats{}
	}
	h.peers.mu.Lock()
	defer h.peers.mu.Unlock()
	return h.peers.stats
}

// Subscribe registers a listener; call the returned func to unsubscribe.
func (h *Hub) Subscribe(sessionID string) (<-chan []byte, func()) {
	ch := make(chan []byte, 256)
	h.mu.Lock()
	m := h.subs[sessionID]
	if m == nil {
		m = map[chan []byte]struct{}{}
		h.subs[sessionID] = m
	}
	m[ch] = struct{}{}
	h.mu.Unlock()
	return ch, func() {
		h.mu.Lock()
		if m := h.subs[sessionID]; m != nil {
			delete(m, ch)
			if len(m) == 0 {
				delete(h.subs, sessionID)
			}
		}
		h.mu.Unlock()
	}
}

func (h *Hub) Listeners(sessionID string) int {
	h.mu.RLock()
	defer h.mu.RUnlock()
	return len(h.subs[sessionID])
}

func (h *Hub) publish(sessionID string, frame []byte) {
	h.mu.RLock()
	defer h.mu.RUnlock()
	for ch := range h.subs[sessionID] {
		select {
		case ch <- frame:
		default: // slow listener: drop rather than block the relay
		}
	}
}

// ListenUDP serves the ingest socket until it fails.
func (h *Hub) ListenUDP(addr string) error {
	ua, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return err
	}
	conn, err := net.ListenUDP("udp", ua)
	if err != nil {
		return err
	}
	_ = conn.SetReadBuffer(4 << 20)
	_ = conn.SetWriteBuffer(4 << 20)
	log.Printf("gigma audio relay listening on udp %s (browser ingest + AOO peer relay)", addr)
	if h.peers != nil {
		go func() {
			for range time.Tick(10 * time.Second) {
				h.peers.gc(time.Now())
			}
		}()
	}
	buf := make([]byte, 65536)
	var lastSess, lastHost string
	lastOK := false
	for {
		n, from, err := conn.ReadFromUDP(buf)
		if err != nil {
			return err
		}
		if h.peers != nil && n >= 4 && bytes.Equal(buf[:4], peerMagic) {
			if to, out := h.peers.handle(buf[:n], from, time.Now()); to != nil {
				_, _ = conn.WriteToUDP(out, to)
			}
			continue
		}
		if n <= headerLen || !bytes.Equal(buf[:4], magic) {
			continue
		}
		sid := string(buf[4:20])
		hid := string(buf[20:36])
		if sid != lastSess || hid != lastHost {
			lastSess, lastHost, lastOK = sid, hid, h.auth(sid, hid)
		}
		if !lastOK {
			// re-check occasionally in case the host claimed after first packet
			lastOK = h.auth(sid, hid)
			if !lastOK {
				continue
			}
		}
		if h.Listeners(sid) == 0 {
			continue
		}
		frame := make([]byte, n-36)
		copy(frame, buf[36:n])
		h.publish(sid, frame)
	}
}
