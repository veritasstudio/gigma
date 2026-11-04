package pluginreg

import (
	"crypto/rand"
	"encoding/hex"
	"sync"
	"time"
)

// Registry tracks producer DAW plugins that own a session's audio endpoint.
type Host struct {
	SessionID string    `json:"sessionId"`
	HostID    string    `json:"hostId"`
	Label     string    `json:"label"`
	SeenAt    time.Time `json:"seenAt"`
}

type Registry struct {
	mu     sync.RWMutex
	bySess map[string]*Host
}

func New() *Registry {
	return &Registry{bySess: map[string]*Host{}}
}

func (r *Registry) Claim(sessionID, label string) *Host {
	h := &Host{
		SessionID: sessionID,
		HostID:    newID(),
		Label:     label,
		SeenAt:    time.Now().UTC(),
	}
	r.mu.Lock()
	r.bySess[sessionID] = h
	r.mu.Unlock()
	return h
}

func (r *Registry) Heartbeat(sessionID, hostID string) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	h, ok := r.bySess[sessionID]
	if !ok || h.HostID != hostID {
		return false
	}
	h.SeenAt = time.Now().UTC()
	return true
}

func (r *Registry) Get(sessionID string) (*Host, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	h, ok := r.bySess[sessionID]
	return h, ok
}

func newID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}
