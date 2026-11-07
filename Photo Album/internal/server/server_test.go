package server

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"testing/fstest"
	"time"

	"photo-album/internal/app"
	"photo-album/internal/fsbrowse"
	"photo-album/internal/importer"
	"photo-album/internal/scheduler"
)

func do(t *testing.T, h http.Handler, method, url string, body any) (int, map[string]any) {
	t.Helper()
	var rd io.Reader
	if body != nil {
		b, _ := json.Marshal(body)
		rd = bytes.NewReader(b)
	}
	req := httptest.NewRequest(method, url, rd)
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)
	var m map[string]any
	_ = json.Unmarshal(rec.Body.Bytes(), &m)
	return rec.Code, m
}

func waitPhase(t *testing.T, h http.Handler, want string) map[string]any {
	t.Helper()
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		_, m := do(t, h, "GET", "/api/status", nil)
		if m["phase"] == want {
			return m
		}
		time.Sleep(20 * time.Millisecond)
	}
	t.Fatalf("phase %s not reached", want)
	return nil
}

func TestEndToEnd(t *testing.T) {
	share, _ := filepath.EvalSymlinks(t.TempDir())
	data := t.TempDir()
	album := filepath.Join(share, "Photos")
	inbox := filepath.Join(share, "Inbox")
	os.MkdirAll(filepath.Join(album, "old"), 0o755)
	os.WriteFile(filepath.Join(album, "old", "a.jpg"), []byte("existing"), 0o644)
	os.MkdirAll(inbox, 0o755)

	a := app.New(data, share, time.Local)
	a.RetryEvery = 50 * time.Millisecond
	a.Tune = func(im *importer.Importer, s *scheduler.Scheduler) {
		im.WriteWait = 0
		s.Tick = 20 * time.Millisecond
	}
	static := fstest.MapFS{"index.html": {Data: []byte("<html>spa</html>")}, "assets/app.js": {Data: []byte("js")}}
	h := New(a, &fsbrowse.Browser{Root: share}, static)

	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() { a.Run(ctx); close(done) }()
	defer func() { cancel(); <-done }()

	waitPhase(t, h, "unconfigured")

	// Directory listing.
	code, m := do(t, h, "GET", "/api/fs/list", nil)
	if code != 200 || len(m["dirs"].([]any)) != 2 {
		t.Fatal(code, m)
	}
	if code, _ := do(t, h, "GET", "/api/fs/list?path=/etc", nil); code != 400 {
		t.Fatal(code)
	}

	// Invalid setup is rejected with messages.
	code, m = do(t, h, "POST", "/api/setup", map[string]any{"album_dir": album, "import_dir": album, "interval_minutes": 60})
	if code != 400 || !strings.Contains(m["error"].(string), "不能相同") {
		t.Fatal(code, m)
	}

	// Valid setup.
	os.WriteFile(filepath.Join(inbox, "new.jpg"), []byte("fresh"), 0o644)
	os.WriteFile(filepath.Join(inbox, "dup.jpg"), []byte("existing"), 0o644)
	code, m = do(t, h, "POST", "/api/setup", map[string]any{"album_dir": album, "import_dir": inbox, "interval_minutes": 60})
	if code != 200 {
		t.Fatal(code, m)
	}
	if _, err := os.Stat(filepath.Join(data, "bootstrap.json")); err != nil {
		t.Fatal(err)
	}
	if code, _ := do(t, h, "POST", "/api/setup", map[string]any{"album_dir": album, "import_dir": inbox, "interval_minutes": 60}); code != 409 {
		t.Fatal("second setup must be rejected", code)
	}

	st := waitPhase(t, h, "running")
	if st["index_total"].(float64) != 1 || st["initial_index_done"] != true {
		t.Fatal(st)
	}

	// The first scan runs right after the initial index.
	deadline := time.Now().Add(10 * time.Second)
	var newPath string
	for time.Now().Before(deadline) && newPath == "" {
		filepath.Walk(album, func(p string, info os.FileInfo, err error) error {
			if err == nil && info.Name() == "new.jpg" {
				newPath = p
			}
			return nil
		})
		time.Sleep(20 * time.Millisecond)
	}
	if newPath == "" {
		t.Fatal("new.jpg not imported")
	}
	for time.Now().Before(deadline) {
		if _, m := do(t, h, "GET", "/api/status", nil); m["last_scan_finished_at"] != nil {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	dups, _ := filepath.Glob(filepath.Join(inbox, "duplicates", "*", "dup.jpg"))
	if len(dups) != 1 {
		t.Fatal("dup.jpg not moved to duplicates")
	}
	logs, _ := filepath.Glob(filepath.Join(inbox, "logs", "*.log"))
	if len(logs) != 1 {
		t.Fatal("no log")
	}

	// Interval change.
	if code, _ := do(t, h, "PUT", "/api/settings/interval", map[string]any{"interval_minutes": 3}); code != 400 {
		t.Fatal(code)
	}
	code, m = do(t, h, "PUT", "/api/settings/interval", map[string]any{"interval_minutes": 15})
	if code != 200 || m["interval_minutes"].(float64) != 15 {
		t.Fatal(code, m)
	}
	next, _ := time.Parse(time.RFC3339, m["next_scan_at"].(string))
	if d := time.Until(next); d < 14*time.Minute || d > 16*time.Minute {
		t.Fatal(d)
	}

	// SPA fallback.
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest("GET", "/status", nil))
	if rec.Body.String() != "<html>spa</html>" {
		t.Fatal(rec.Body.String())
	}
	rec = httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest("GET", "/assets/app.js", nil))
	if rec.Body.String() != "js" {
		t.Fatal(rec.Body.String())
	}
}

func TestStorageUnavailable(t *testing.T) {
	data := t.TempDir()
	os.WriteFile(filepath.Join(data, "bootstrap.json"), []byte(`{"album_dir":"/nonexistent/album"}`), 0o644)
	a := app.New(data, "/share", time.Local)
	a.RetryEvery = 20 * time.Millisecond
	h := New(a, &fsbrowse.Browser{Root: "/share"}, fstest.MapFS{})
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() { a.Run(ctx); close(done) }()
	defer func() { cancel(); <-done }()
	m := waitPhase(t, h, "storage_unavailable")
	if m["error"] == nil {
		t.Fatal(m)
	}
	if code, _ := do(t, h, "POST", "/api/setup", map[string]any{}); code != 409 {
		t.Fatal("setup must not be possible when storage is unavailable", code)
	}
}
