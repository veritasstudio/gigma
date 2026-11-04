package relay

// Browser mic → everyone (guest talk-back).
//
// A browser guest's listen WebSocket is bidirectional: after the guest unmutes,
// it sends one binary frame per 20 ms of mic audio up the same socket. The
// broker does no audio processing; it only fans each frame out:
//
//   - to every other browser listener of the session (WebSocket), and
//   - to every plugin of the session currently registered on the AOO peer relay
//     (the host, plus plugin guests), as a "GGB1" UDP datagram sent from the
//     relay port — the same socket/NAT mapping their GGR1 keepalives hold open,
//     so plugins need no new port or address.
//
// Browser → broker (WebSocket binary):
//
//	0 codec u8 (1 = Opus 48 kHz mono, one 20 ms packet; 2 = PCM16 LE 16 kHz mono)
//	1..3 reserved | 4 seq u32 LE | 8 payload
//
// Broker → other browsers (WebSocket binary, 12-byte header like host frames,
// but sampleRate == 0 marks a mic frame):
//
//	0 seq u32 | 4 0 u32 | 8 codec u8 | 9 0 | 10 talker u16 LE | 12 payload
//
// Broker → plugins (UDP):
//
//	0 "GGB1" | 4 talker u16 LE | 6 codec u8 | 7 0 | 8 seq u32 LE | 12 payload
//
// Talker ids are small per-session integers assigned per WebSocket; names are
// exposed via Talkers() (session API + host heartbeat).

import (
	"encoding/binary"
	"net"
	"sort"
	"sync"
	"time"
)

const (
	MicCodecOpus  = 1
	MicCodecPCM16 = 2
	micHdrIn      = 8
	micHdrOut     = 12
	micMaxPayload = 4000
)

var micMagic = []byte("GGB1")

// Talker is a browser guest that can send mic audio.
type Talker struct {
	ID     uint16    `json:"id"`
	Name   string    `json:"name"`
	Live   bool      `json:"live"`  // mic frames in the last second (unmuted + flowing)
	Muted  bool      `json:"muted"` // no frames for > 1 s
	LastAt time.Time `json:"-"`
	sub    chan []byte
	frames uint64
}

type micState struct {
	mu      sync.Mutex
	next    map[string]uint16
	talkers map[string]map[uint16]*Talker
	conn    *net.UDPConn
	sent    uint64
}

func (h *Hub) mic() *micState {
	h.micOnce.Do(func() {
		h.micSt = &micState{next: map[string]uint16{}, talkers: map[string]map[uint16]*Talker{}}
	})
	return h.micSt
}

// SubscribeTalker registers a bidirectional browser guest: like Subscribe, plus
// a talker id for frames it sends with Talk. Call the returned func on close.
func (h *Hub) SubscribeTalker(sessionID, name string) (uint16, <-chan []byte, func()) {
	frames, unsub := h.subscribe(sessionID)
	m := h.mic()
	m.mu.Lock()
	id := m.next[sessionID] + 1
	if id == 0 {
		id = 1
	}
	m.next[sessionID] = id
	ts := m.talkers[sessionID]
	if ts == nil {
		ts = map[uint16]*Talker{}
		m.talkers[sessionID] = ts
	}
	if len(name) > 40 {
		name = name[:40]
	}
	ts[id] = &Talker{ID: id, Name: name, sub: frames}
	m.mu.Unlock()
	return id, frames, func() {
		unsub()
		m.mu.Lock()
		if ts := m.talkers[sessionID]; ts != nil {
			delete(ts, id)
			if len(ts) == 0 {
				delete(m.talkers, sessionID)
				delete(m.next, sessionID)
			}
		}
		m.mu.Unlock()
	}
}

// Talkers lists browser guests of a session (connected to the listen socket).
func (h *Hub) Talkers(sessionID string) []Talker {
	m := h.mic()
	now := time.Now()
	m.mu.Lock()
	defer m.mu.Unlock()
	out := []Talker{}
	for _, t := range m.talkers[sessionID] {
		c := *t
		c.Live = now.Sub(t.LastAt) < time.Second
		c.Muted = !c.Live
		c.sub = nil
		out = append(out, c)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// Talk fans one browser mic frame out to other browsers and to the session's
// relayed plugins. Returns false if the frame was malformed.
func (h *Hub) Talk(sessionID string, talker uint16, in []byte) bool {
	if len(in) <= micHdrIn || len(in) > micHdrIn+micMaxPayload {
		return false
	}
	codec := in[0]
	if codec != MicCodecOpus && codec != MicCodecPCM16 {
		return false
	}
	seq := binary.LittleEndian.Uint32(in[4:8])
	payload := in[micHdrIn:]

	m := h.mic()
	m.mu.Lock()
	var self chan []byte
	if t := m.talkers[sessionID][talker]; t != nil {
		t.LastAt = time.Now()
		t.frames++
		self = t.sub
	}
	conn := m.conn
	m.mu.Unlock()

	// Browsers (everyone but the speaker).
	ws := make([]byte, micHdrOut+len(payload))
	binary.LittleEndian.PutUint32(ws[0:], seq)
	ws[8] = codec
	binary.LittleEndian.PutUint16(ws[10:], talker)
	copy(ws[micHdrOut:], payload)
	h.mu.RLock()
	for ch := range h.subs[sessionID] {
		if ch == self {
			continue
		}
		select {
		case ch <- ws:
		default:
		}
	}
	h.mu.RUnlock()

	// Plugins on the AOO relay (host + plugin guests).
	if conn == nil || h.peers == nil {
		return true
	}
	udp := make([]byte, micHdrOut+len(payload))
	copy(udp, micMagic)
	binary.LittleEndian.PutUint16(udp[4:], talker)
	udp[6] = codec
	binary.LittleEndian.PutUint32(udp[8:], seq)
	copy(udp[micHdrOut:], payload)
	for _, a := range h.peers.addrs(sessionID, time.Now()) {
		_, _ = conn.WriteToUDP(udp, a)
		m.mu.Lock()
		m.sent++
		m.mu.Unlock()
	}
	return true
}

// MicSent counts GGB1 datagrams sent to plugins (for /healthz and tests).
func (h *Hub) MicSent() uint64 {
	m := h.mic()
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.sent
}

func (t *peerTable) addrs(sid string, now time.Time) []*net.UDPAddr {
	t.mu.Lock()
	defer t.mu.Unlock()
	var out []*net.UDPAddr
	for _, p := range t.routes[sid] {
		if now.Sub(p.seenAt) <= peerMaxAge {
			out = append(out, p.addr)
		}
	}
	return out
}
