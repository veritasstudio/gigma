package main

import (
	"context"
	"encoding/json"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"github.com/veritasstudio/gigma/internal/session"
	"github.com/veritasstudio/gigma/internal/sonobus"
)

func main() {
	addr := env("GIGMA_ADDR", ":8080")
	publicBase := strings.TrimRight(env("GIGMA_PUBLIC_BASE", "http://localhost:8080"), "/")
	bin := env("SONOBUS_BIN", "sonobus")
	webDir := env("GIGMA_WEB", "web/static")

	store := session.NewStore()
	sb := sonobus.NewManager(bin)

	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{
			"ok":      true,
			"sonobus": sb.Status(),
		})
	})

	mux.HandleFunc("POST /api/sessions", func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Title string `json:"title"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		s := store.Create(body.Title)
		if err := sb.EnsureRunning(s.ID); err != nil {
			log.Printf("sonobus start (session %s): %v", s.ID, err)
		}
		writeJSON(w, http.StatusCreated, map[string]any{
			"id":   s.ID,
			"title": s.Title,
			"path": "/m/" + s.ID,
			"url":  publicBase + "/m/" + s.ID,
		})
	})

	mux.HandleFunc("GET /api/sessions/{id}", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		s, ok := store.Get(id)
		if !ok {
			http.Error(w, "session not found", http.StatusNotFound)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"id":      s.ID,
			"title":   s.Title,
			"path":    "/m/" + s.ID,
			"url":     publicBase + "/m/" + s.ID,
			"sonobus": sb.Status(),
		})
	})

	mux.HandleFunc("POST /api/sonobus/start", func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			SessionID string `json:"sessionId"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		if err := sb.EnsureRunning(body.SessionID); err != nil {
			writeJSON(w, http.StatusBadGateway, map[string]any{"error": err.Error(), "status": sb.Status()})
			return
		}
		writeJSON(w, http.StatusOK, sb.Status())
	})

	mux.HandleFunc("POST /api/sonobus/stop", func(w http.ResponseWriter, r *http.Request) {
		sb.Stop()
		writeJSON(w, http.StatusOK, sb.Status())
	})

	mux.HandleFunc("GET /api/sonobus/status", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, sb.Status())
	})

	// Meeting pages — Zoom-style join via link
	mux.HandleFunc("GET /m/{id}", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		if _, ok := store.Get(id); !ok {
			http.NotFound(w, r)
			return
		}
		http.ServeFile(w, r, filepath.Join(webDir, "meeting.html"))
	})

	mux.Handle("GET /", http.FileServer(http.Dir(webDir)))

	srv := &http.Server{Addr: addr, Handler: logging(mux)}
	go func() {
		log.Printf("gigma listening on %s (public base %s)", addr, publicBase)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatal(err)
		}
	}()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	<-stop
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	sb.Stop()
	_ = srv.Shutdown(ctx)
}

func env(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func logging(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		log.Printf("%s %s %s", r.Method, r.URL.Path, time.Since(start))
	})
}
