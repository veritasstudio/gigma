package session

import (
	"crypto/rand"
	"encoding/hex"
	"sync"
	"time"
)

type Session struct {
	ID        string    `json:"id"`
	Title     string    `json:"title"`
	CreatedAt time.Time `json:"createdAt"`
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
	return sess, ok
}

func newID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}
