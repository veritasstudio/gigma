package session

import (
	"crypto/rand"
	"encoding/hex"
	"sync"
	"time"
)

type Guest struct {
	ID       string    `json:"id"`
	Name     string    `json:"name"`
	JoinedAt time.Time `json:"joinedAt"`
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

func (s *Store) Join(id, name string) (*Guest, *Session, bool) {
	if name == "" {
		name = "Guest"
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.byID[id]
	if !ok {
		return nil, nil, false
	}
	g := Guest{ID: newID(), Name: name, JoinedAt: time.Now().UTC()}
	sess.Guests = append(sess.Guests, g)
	cp := *sess
	cp.Guests = append([]Guest(nil), sess.Guests...)
	return &g, &cp, true
}

func newID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}
