package session

import (
	"crypto/rand"
	"encoding/hex"
	"sync"
	"time"
)

// Guest is anyone who opened the meeting link. Browser guests listen through
// the broker's WebSocket relay; plugin guests advertise an AOO UDP endpoint so
// the host plugin can addSink() them directly.
type Guest struct {
	ID          string    `json:"id"`
	Name        string    `json:"name"`
	Kind        string    `json:"kind"`                  // "browser" | "plugin"
	AOOEndpoint string    `json:"aooEndpoint,omitempty"` // LAN "ip:port" (direct path)
	Token       string    `json:"-"`                     // secret for the AOO relay; returned once on join
	JoinedAt    time.Time `json:"joinedAt"`
	SeenAt      time.Time `json:"seenAt"`
}

type Session struct {
	ID        string    `json:"id"`
	Title     string    `json:"title"`
	CreatedAt time.Time `json:"createdAt"`
	Guests    []Guest   `json:"guests"`
}

type Store struct {
	mu   sync.RWMutex
	byID map[string]*Session
}

func NewStore() *Store {
	return &Store{byID: map[string]*Session{}}
}

func (s *Store) Create(title string) *Session {
	if title == "" {
		title = "Gigma session"
	}
	sess := &Session{
		ID:        newID(),
		Title:     title,
		CreatedAt: time.Now().UTC(),
		Guests:    nil,
	}
	s.mu.Lock()
	s.byID[sess.ID] = sess
	s.mu.Unlock()
	return sess
}

func (s *Store) Get(id string) (*Session, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	sess, ok := s.byID[id]
	if !ok {
		return nil, false
	}
	cp := *sess
	cp.Guests = append([]Guest(nil), sess.Guests...)
	return &cp, true
}

func (s *Store) Join(id, name, kind, aooEndpoint string) (*Guest, *Session, bool) {
	if name == "" {
		name = "Guest"
	}
	if kind != "plugin" {
		kind = "browser"
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.byID[id]
	if !ok {
		return nil, nil, false
	}
	now := time.Now().UTC()
	g := Guest{ID: newID(), Name: name, Kind: kind, AOOEndpoint: aooEndpoint, Token: newID(), JoinedAt: now, SeenAt: now}
	sess.Guests = append(sess.Guests, g)
	cp := *sess
	cp.Guests = append([]Guest(nil), sess.Guests...)
	return &g, &cp, true
}

// Touch refreshes a guest's liveness and (optionally) its AOO endpoint.
func (s *Store) Touch(id, guestID, aooEndpoint string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.byID[id]
	if !ok {
		return false
	}
	for i := range sess.Guests {
		if sess.Guests[i].ID == guestID {
			sess.Guests[i].SeenAt = time.Now().UTC()
			if aooEndpoint != "" {
				sess.Guests[i].AOOEndpoint = aooEndpoint
			}
			return true
		}
	}
	return false
}

// GuestByToken resolves a plugin guest's relay secret to its public guest ID.
func (s *Store) GuestByToken(id, token string) (string, bool) {
	if token == "" {
		return "", false
	}
	s.mu.RLock()
	defer s.mu.RUnlock()
	sess, ok := s.byID[id]
	if !ok {
		return "", false
	}
	for _, g := range sess.Guests {
		if g.Kind == "plugin" && g.Token == token {
			return g.ID, true
		}
	}
	return "", false
}

// PluginPeers returns live plugin guests (reachable over the relay by ID,
// and directly via AOOEndpoint on a shared LAN/VPN) seen within maxAge.
func (s *Store) PluginPeers(id string, maxAge time.Duration) []Guest {
	s.mu.RLock()
	defer s.mu.RUnlock()
	sess, ok := s.byID[id]
	if !ok {
		return nil
	}
	cutoff := time.Now().Add(-maxAge)
	out := []Guest{}
	for _, g := range sess.Guests {
		if g.Kind == "plugin" && g.SeenAt.After(cutoff) {
			out = append(out, g)
		}
	}
	return out
}

func newID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}
