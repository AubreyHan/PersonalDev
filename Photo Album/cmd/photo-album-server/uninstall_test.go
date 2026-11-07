package main

import (
	"os"
	"path/filepath"
	"testing"

	"photo-album/internal/bootstrap"
)

func TestUninstallCleanup(t *testing.T) {
	data, album := t.TempDir(), t.TempDir()
	os.MkdirAll(filepath.Join(album, ".photo-album"), 0o755)
	os.WriteFile(filepath.Join(album, ".photo-album", "photo-album.db"), []byte("db"), 0o644)
	os.MkdirAll(filepath.Join(album, "2026"), 0o755)
	os.WriteFile(filepath.Join(album, "2026", "a.jpg"), []byte("photo"), 0o644)
	bootstrap.Save(data, bootstrap.Config{AlbumDir: album})

	if err := uninstallCleanup(data); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(album, ".photo-album")); !os.IsNotExist(err) {
		t.Fatal("index not removed")
	}
	if _, err := os.Stat(filepath.Join(album, "2026", "a.jpg")); err != nil {
		t.Fatal("photo removed!")
	}
	if err := uninstallCleanup(data); err != nil { // idempotent: now unconfigured
		t.Fatal(err)
	}
}
