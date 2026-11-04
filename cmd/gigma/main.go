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

	"github.com/coder/websocket"
	"github.com/veritasstudio/gigma/internal/pluginreg"
	"github.com/veritasstudio/gigma/internal/relay"
	"github.com/veritasstudio/gigma/internal/session"
)

const peerMaxAge = 30 * time.Second

func main() {
	addr := env("GIGMA_ADDR", ":8080")
	publicBase := strings.TrimRight(env("GIGMA_PUBLIC_BASE", "https://gigma.veritas.org.in"), "/")
	webDir := env("GIGMA_WEB", "web/static")

	store := session.NewStore()
	plugins := pluginreg.New()
	// UDP ingest for host→browser audio. Public address is what the plugin
	// sends to (the VPS IP; the HTTP hostname sits behind Cloudflare).
	relayAddr := env("GIGMA_RELAY_UDP", ":7300")
	relayPublic := os.Getenv("GIGMA_RELAY_PUBLIC")
	hub := relay.New(func(sid, hid string) bool {
		h, ok := plugins.Get(sid)
		return ok && h.HostID == hid
	})
	if relayAddr != "off" {
		go func() {
			if err := hub.ListenUDP(relayAddr); err != nil {
				log.Printf("relay disabled: %v", err)
			}
		}()
	}

	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{
			"ok":     true,
			"role":   "meeting-link-broker",
			"audio":  "plugin-owned (AOO p2p) + browser relay",
			"relay":  relayPublic,
			"public": publicBase,
		})
	})

	mux.HandleFunc("POST /api/sessions", func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Title string `json:"title"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		s := store.Create(body.Title)
		writeJSON(w, http.StatusCreated, map[string]any{
			"id":    s.ID,
			"title": s.Title,
			"path":  "/m/" + s.ID,
			"url":   publicBase + "/m/" + s.ID,
		})
	})

	mux.HandleFunc("GET /api/sessions/{id}", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		s, ok := store.Get(id)
		if !ok {
			http.Error(w, "session not found", http.StatusNotFound)
			return
		}
		host, hasHost := plugins.Get(id)
		resp := map[string]any{
			"id":        s.ID,
			"title":     s.Title,
			"path":      "/m/" + s.ID,
			"url":       publicBase + "/m/" + s.ID,
			"open":      true,
			"guests":    s.Guests,
			"hasHost":   hasHost,
			"listeners": hub.Listeners(id),
		}
		if hasHost {
			resp["host"] = host
			resp["hostLive"] = time.Since(host.SeenAt) < peerMaxAge
		}
		writeJSON(w, http.StatusOK, resp)
	})

	mux.HandleFunc("POST /api/sessions/{id}/join", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		var body struct {
			Name        string `json:"name"`
			Kind        string `json:"kind"`
			AOOEndpoint string `json:"aooEndpoint"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		g, s, ok := store.Join(id, body.Name, body.Kind, body.AOOEndpoint)
		if !ok {
			http.Error(w, "session not found", http.StatusNotFound)
			return
		}
		host, hasHost := plugins.Get(id)
		resp := map[string]any{
			"guest":   g,
			"session": map[string]any{"id": s.ID, "title": s.Title, "guests": s.Guests},
			"hasHost": hasHost,
		}
		if hasHost {
			resp["host"] = host
			resp["aooEndpoint"] = host.AOOEndpoint
		}
		writeJSON(w, http.StatusOK, resp)
	})

	mux.HandleFunc("POST /api/sessions/{id}/host", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		if _, ok := store.Get(id); !ok {
			http.Error(w, "session not found", http.StatusNotFound)
			return
		}
		var body struct {
			Label       string `json:"label"`
			AOOEndpoint string `json:"aooEndpoint"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		if body.Label == "" {
			body.Label = "Gigma Plugin"
		}
		h := plugins.Claim(id, body.Label, body.AOOEndpoint)
		writeJSON(w, http.StatusOK, map[string]any{
			"host":  h,
			"url":   publicBase + "/m/" + id,
			"relay": relayPublic,
		})
	})

	mux.HandleFunc("POST /api/sessions/{id}/host/heartbeat", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		var body struct {
			HostID      string `json:"hostId"`
			AOOEndpoint string `json:"aooEndpoint"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		if !plugins.Heartbeat(id, body.HostID, body.AOOEndpoint) {
			http.Error(w, "unknown host", http.StatusConflict)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"ok":        true,
			"peers":     store.PluginPeers(id, peerMaxAge),
			"listeners": hub.Listeners(id),
			"relay":     relayPublic,
		})
	})

	// Plugin guests keep their AOO endpoint fresh and learn the host's.
	mux.HandleFunc("POST /api/sessions/{id}/guests/{gid}/heartbeat", func(w http.ResponseWriter, r *http.Request) {
		id, gid := r.PathValue("id"), r.PathValue("gid")
		var body struct {
			AOOEndpoint string `json:"aooEndpoint"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		if !store.Touch(id, gid, body.AOOEndpoint) {
			http.Error(w, "unknown guest", http.StatusNotFound)
			return
		}
		resp := map[string]any{"ok": true}
		if host, ok := plugins.Get(id); ok {
			resp["aooEndpoint"] = host.AOOEndpoint
			resp["hostLive"] = time.Since(host.SeenAt) < peerMaxAge
		}
		writeJSON(w, http.StatusOK, resp)
	})

	// Browser guests: PCM16 frames relayed from the host plugin.
	mux.HandleFunc("GET /api/sessions/{id}/listen", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		if _, ok := store.Get(id); !ok {
			http.Error(w, "session not found", http.StatusNotFound)
			return
		}
		c, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
		if err != nil {
			return
		}
		defer c.CloseNow()
		frames, unsub := hub.Subscribe(id)
		defer unsub()
		ctx := c.CloseRead(r.Context())
		for {
			select {
			case <-ctx.Done():
				return
			case f := <-frames:
				wctx, cancel := context.WithTimeout(ctx, 2*time.Second)
				err := c.Write(wctx, websocket.MessageBinary, f)
				cancel()
				if err != nil {
					return
				}
			}
		}
	})

	mux.HandleFunc("GET /m/{id}", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		if _, ok := store.Get(id); !ok {
			http.NotFound(w, r)
			return
		}
		http.ServeFile(w, r, filepath.Join(webDir, "meeting.html"))
	})

	mux.HandleFunc("GET /m/{id}/room", func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		if _, ok := store.Get(id); !ok {
			http.NotFound(w, r)
			return
		}
		http.ServeFile(w, r, filepath.Join(webDir, "room.html"))
	})

	mux.Handle("GET /", http.FileServer(http.Dir(webDir)))

	srv := &http.Server{Addr: addr, Handler: withCORS(logging(mux))}
	go func() {
		log.Printf("gigma broker listening on %s (public base %s)", addr, publicBase)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatal(err)
		}
	}()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	<-stop
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
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
		if strings.HasSuffix(r.URL.Path, "/heartbeat") {
			return
		}
		log.Printf("%s %s %s", r.Method, r.URL.Path, time.Since(start))
	})
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}
