package indexer

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"photo-album/internal/hashing"
	"photo-album/internal/store"
)

func put(t *testing.T, root, rel, content string) {
	t.Helper()
	p := filepath.Join(root, rel)
	os.MkdirAll(filepath.Dir(p), 0o755)
	if err := os.WriteFile(p, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestInitialResumeAndIncremental(t *testing.T) {
	album := t.TempDir()
	for i, rel := range []string{"a.jpg", "b/c.heic", "b/d.mov", "e.txt", "@Recycle/x.jpg"} {
		put(t, album, rel, string(rune('a'+i)))
	}
	put(t, album, "b/.photo-album-tmp-abc", "leftover")
	s, err := store.Open(album, true)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()

	// First run is interrupted after hashing one file.
	ix := New(s, album)
	calls := 0
	ctx, cancel := context.WithCancel(context.Background())
	ix.Hash = func(p string) (string, error) {
		calls++
		if calls == 2 {
			cancel()
		}
		return hashing.MD5File(p)
	}
	if err := ix.Initial(ctx); !errors.Is(err, context.Canceled) {
		t.Fatalf("want canceled, got %v", err)
	}
	if done, _ := s.GetBool(store.KeyInitialIndexDone); done {
		t.Fatal("should not be done")
	}
	if _, err := os.Stat(filepath.Join(album, "b/.photo-album-tmp-abc")); !os.IsNotExist(err) {
		t.Fatal("temp file not cleaned")
	}

	// Resume: only the remaining files are hashed.
	calls = 0
	ix.Hash = func(p string) (string, error) { calls++; return hashing.MD5File(p) }
	if err := ix.Initial(context.Background()); err != nil {
		t.Fatal(err)
	}
	if n, _ := s.CountFiles(); n != 3 {
		t.Fatalf("count=%d", n)
	}
	if calls != 1 {
		t.Fatalf("resume re-hashed %d files, want 1", calls)
	}
	if total, _ := s.GetInt(store.KeyIndexTotal); total != 3 {
		t.Fatal(total)
	}
	if done, _ := s.GetInt(store.KeyIndexDone); done != 3 {
		t.Fatal(done)
	}

	// Incremental: add, modify, delete.
	put(t, album, "new.png", "new")
	put(t, album, "a.jpg", "changed content")
	future := time.Now().Add(time.Hour)
	os.Chtimes(filepath.Join(album, "a.jpg"), future, future)
	os.Remove(filepath.Join(album, "b/d.mov"))
	if err := ix.Incremental(context.Background()); err != nil {
		t.Fatal(err)
	}
	files, _ := s.AllFiles()
	if len(files) != 3 {
		t.Fatalf("files=%v", files)
	}
	want, _ := hashing.MD5File(filepath.Join(album, "a.jpg"))
	if files["a.jpg"].MD5 != want {
		t.Fatal("modified file not re-hashed")
	}
	if _, ok := files["new.png"]; !ok {
		t.Fatal("new file missing")
	}
	if rel, ok, _ := s.FindByMD5(want); !ok || rel != "a.jpg" {
		t.Fatal(rel, ok)
	}
}
