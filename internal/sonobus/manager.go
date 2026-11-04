package sonobus

import (
	"fmt"
	"os"
	"os/exec"
	"sync"
	"time"
)

// Manager launches SonoBus as an external child process.
// It never links against SonoBus; the binary path is configurable.
type Manager struct {
	bin string

	mu      sync.Mutex
	cmd     *exec.Cmd
	session string
	started time.Time
	lastErr string
}

func NewManager(bin string) *Manager {
	return &Manager{bin: bin}
}

type Status struct {
	Running   bool   `json:"running"`
	Binary    string `json:"binary"`
	SessionID string `json:"sessionId,omitempty"`
	PID       int    `json:"pid,omitempty"`
	StartedAt string `json:"startedAt,omitempty"`
	LastError string `json:"lastError,omitempty"`
	Note      string `json:"note,omitempty"`
}

func (m *Manager) Status() Status {
	m.mu.Lock()
	defer m.mu.Unlock()
	st := Status{Binary: m.bin, SessionID: m.session, LastError: m.lastErr}
	if m.cmd != nil && m.cmd.Process != nil {
		st.Running = true
		st.PID = m.cmd.Process.Pid
		st.StartedAt = m.started.UTC().Format(time.RFC3339)
	} else {
		st.Note = "SonoBus is managed as a child process only; set SONOBUS_BIN to the installed binary."
	}
	return st
}

func (m *Manager) EnsureRunning(sessionID string) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.cmd != nil && m.cmd.Process != nil {
		m.session = sessionID
		return nil
	}
	path, err := exec.LookPath(m.bin)
	if err != nil {
		m.lastErr = fmt.Sprintf("binary not found (%s): %v", m.bin, err)
		// Soft-fail so the web UI still works without SonoBus installed yet.
		return fmt.Errorf("%s", m.lastErr)
	}
	cmd := exec.Command(path)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		m.lastErr = err.Error()
		return err
	}
	m.cmd = cmd
	m.session = sessionID
	m.started = time.Now()
	m.lastErr = ""
	go func() {
		_ = cmd.Wait()
		m.mu.Lock()
		if m.cmd == cmd {
			m.cmd = nil
		}
		m.mu.Unlock()
	}()
	return nil
}

func (m *Manager) Stop() {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.cmd == nil || m.cmd.Process == nil {
		return
	}
	_ = m.cmd.Process.Signal(os.Interrupt)
	done := make(chan struct{})
	go func() {
		_, _ = m.cmd.Process.Wait()
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		_ = m.cmd.Process.Kill()
	}
	m.cmd = nil
}
