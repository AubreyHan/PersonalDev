// Package server exposes the HTTP API and serves the embedded front end
// (ARCHITECTURE §6).
package server

import (
	"encoding/json"
	"errors"
	"io/fs"
	"net/http"
	"path"
	"strings"

	"photo-album/internal/app"
	"photo-album/internal/fsbrowse"
	"photo-album/internal/setup"
)

// Server is the HTTP handler.
type Server struct {
	App    *app.App
	Browse *fsbrowse.Browser
	Static fs.FS // front-end build (index.html at root)
	mux    *http.ServeMux
}

// New builds the handler.
func New(a *app.App, browse *fsbrowse.Browser, static fs.FS) *Server {
	s := &Server{App: a, Browse: browse, Static: static, mux: http.NewServeMux()}
	s.mux.HandleFunc("GET /api/ping", func(w http.ResponseWriter, r *http.Request) { writeJSON(w, 200, map[string]string{"ok": "pong"}) })
	s.mux.HandleFunc("GET /api/state", s.state)
	s.mux.HandleFunc("GET /api/status", s.status)
	s.mux.HandleFunc("GET /api/fs/list", s.list)
	s.mux.HandleFunc("POST /api/setup", s.setup)
	s.mux.HandleFunc("PUT /api/settings/interval", s.interval)
	s.mux.HandleFunc("/api/", func(w http.ResponseWriter, r *http.Request) { writeErr(w, 404, "not found") })
	s.mux.HandleFunc("/", s.spa)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.mux.ServeHTTP(w, r) }

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, code int, msgs ...string) {
	writeJSON(w, code, map[string]any{"error": strings.Join(msgs, "；"), "messages": msgs})
}

func (s *Server) state(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, 200, map[string]any{"phase": s.App.Phase()})
}

func (s *Server) status(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, 200, s.App.Status())
}

func (s *Server) list(w http.ResponseWriter, r *http.Request) {
	dirs, err := s.Browse.List(r.URL.Query().Get("path"))
	if err != nil {
		code := 500
		if errors.Is(err, fsbrowse.ErrOutside) {
			code = 400
		} else if errors.Is(err, fs.ErrNotExist) {
			code = 404
		}
		writeErr(w, code, err.Error())
		return
	}
	writeJSON(w, 200, map[string]any{"root": s.Browse.Root, "dirs": dirs})
}

func (s *Server) setup(w http.ResponseWriter, r *http.Request) {
	var req setup.Request
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<16)).Decode(&req); err != nil {
		writeErr(w, 400, "请求格式错误")
		return
	}
	err := s.App.Setup(req)
	var ve *setup.ValidationError
	switch {
	case err == nil:
		writeJSON(w, 200, map[string]bool{"ok": true})
	case errors.As(err, &ve):
		writeErr(w, 400, ve.Messages...)
	case errors.Is(err, app.ErrAlreadyConfigured):
		writeErr(w, 409, err.Error())
	default:
		writeErr(w, 500, err.Error())
	}
}

func (s *Server) interval(w http.ResponseWriter, r *http.Request) {
	var req struct {
		IntervalMinutes int64 `json:"interval_minutes"`
	}
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<12)).Decode(&req); err != nil {
		writeErr(w, 400, "请求格式错误")
		return
	}
	if err := s.App.SetInterval(req.IntervalMinutes); err != nil {
		writeErr(w, 400, err.Error())
		return
	}
	writeJSON(w, 200, s.App.Status())
}

// spa serves static files, falling back to index.html for client routes.
func (s *Server) spa(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		writeErr(w, 405, "method not allowed")
		return
	}
	p := strings.TrimPrefix(path.Clean("/"+r.URL.Path), "/")
	if p != "" {
		if st, err := fs.Stat(s.Static, p); err == nil && !st.IsDir() {
			if strings.HasPrefix(p, "assets/") {
				w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
			}
			http.ServeFileFS(w, r, s.Static, p)
			return
		}
	}
	b, err := fs.ReadFile(s.Static, "index.html")
	if err != nil {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write([]byte("<!doctype html><meta charset=utf-8><title>Photo Album</title><p>前端尚未构建（web/dist 为空）。</p>"))
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-cache")
	w.Write(b)
}
