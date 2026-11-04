package relay

// AOO peer relay (TURN-like, but AOO-aware and tiny).
//
// Plugins behind NAT can't reach each other directly, but every plugin can
// send UDP *out* to the VPS. So each plugin wraps its AOO datagrams in a small
// header naming the destination peer and sends them to the relay port; the
// relay forwards them to that peer's last-seen public address (learned from
// the peer's own outbound packets/keepalives, which also keep its NAT mapping
// open). Works for every NAT type, including symmetric/CGNAT. No port
// forwarding on either side.
//
// Client → relay (UDP, same socket the plugin uses for AOO):
//
//	0  "GGR1"
//	4  sessionId (16 ascii)
//	20 secret    (16 ascii)  host: hostId, plugin guest: join token
//	36 dstPeer   (16 ascii)  "0000000000000000" = keepalive (no payload)
//	52 AOO datagram
//
// Relay → client:
//
//	0  "GGR1"
//	4  srcPeer   (16 ascii)  "0000000000000000" = keepalive ack
//	20 AOO datagram
//
// Peer IDs are public (host: pluginreg.Host.PeerID, guest: session.Guest.ID);
// secrets never leave the broker except to their owner.

import (
	"net"
	"sync"
	"time"
)

const (
	peerHdrIn  = 52
	peerHdrOut = 20
	peerMaxAge = 30 * time.Second
)

var (
	peerMagic = []byte("GGR1")
	zeroID    = "0000000000000000"
)

// PeerAuth resolves (sessionID, secret) to the sender's public peer ID.
type PeerAuth func(sessionID, secret string) (peerID string, ok bool)

type peerAddr struct {
	addr   *net.UDPAddr
	seenAt time.Time
}

type authKey struct{ sid, secret string }

type authEntry struct {
	peerID string
	ok     bool
	at     time.Time
}

type peerTable struct {
	mu     sync.Mutex
	routes map[string]map[string]peerAddr // sid → peerID → addr
	cache  map[authKey]authEntry
	auth   PeerAuth
	stats  PeerStats
}

// PeerStats counts relayed traffic (for /healthz and tests).
type PeerStats struct {
	Forwarded  uint64 `json:"forwarded"`
	Keepalives uint64 `json:"keepalives"`
	Dropped    uint64 `json:"dropped"`
}

func newPeerTable(auth PeerAuth) *peerTable {
	return &peerTable{routes: map[string]map[string]peerAddr{}, cache: map[authKey]authEntry{}, auth: auth}
}

func (t *peerTable) resolve(sid, secret string, now time.Time) (string, bool) {
	k := authKey{sid, secret}
	if e, ok := t.cache[k]; ok && now.Sub(e.at) < 5*time.Second {
		return e.peerID, e.ok
	}
	id, ok := t.auth(sid, secret)
	t.cache[k] = authEntry{id, ok, now}
	if len(t.cache) > 4096 {
		for kk, e := range t.cache {
			if now.Sub(e.at) > 5*time.Second {
				delete(t.cache, kk)
			}
		}
	}
	return id, ok
}

// handle processes one GGR1 datagram; returns (dst, out) to send, or nil.
func (t *peerTable) handle(pkt []byte, from *net.UDPAddr, now time.Time) (*net.UDPAddr, []byte) {
	if len(pkt) < peerHdrIn {
		return nil, nil
	}
	sid := string(pkt[4:20])
	secret := string(pkt[20:36])
	dst := string(pkt[36:52])

	t.mu.Lock()
	defer t.mu.Unlock()
	src, ok := t.resolve(sid, secret, now)
	if !ok {
		t.stats.Dropped++
		return nil, nil
	}
	m := t.routes[sid]
	if m == nil {
		m = map[string]peerAddr{}
		t.routes[sid] = m
	}
	m[src] = peerAddr{addr: from, seenAt: now}

	if dst == zeroID {
		t.stats.Keepalives++
		ack := make([]byte, peerHdrOut)
		copy(ack, peerMagic)
		copy(ack[4:], zeroID)
		return from, ack
	}
	to, ok := m[dst]
	if !ok || now.Sub(to.seenAt) > peerMaxAge || len(pkt) == peerHdrIn {
		t.stats.Dropped++
		return nil, nil
	}
	out := make([]byte, peerHdrOut+len(pkt)-peerHdrIn)
	copy(out, peerMagic)
	copy(out[4:], src)
	copy(out[peerHdrOut:], pkt[peerHdrIn:])
	t.stats.Forwarded++
	return to.addr, out
}

func (t *peerTable) gc(now time.Time) {
	t.mu.Lock()
	defer t.mu.Unlock()
	for sid, m := range t.routes {
		for id, p := range m {
			if now.Sub(p.seenAt) > peerMaxAge {
				delete(m, id)
			}
		}
		if len(m) == 0 {
			delete(t.routes, sid)
		}
	}
}

// RelayedPeers reports which peer IDs of a session are currently reachable
// through the relay (seen within peerMaxAge).
func (t *peerTable) relayed(sid string, now time.Time) map[string]bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	out := map[string]bool{}
	for id, p := range t.routes[sid] {
		if now.Sub(p.seenAt) <= peerMaxAge {
			out[id] = true
		}
	}
	return out
}
