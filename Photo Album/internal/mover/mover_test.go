package mover

import (
	"os"
	"path/filepath"
	"syscall"
	"testing"
	"time"
)

func TestMoveSameDevicePreservesMtime(t *testing.T) {
	d := t.TempDir()
	src := filepath.Join(d, "a.jpg")
	os.WriteFile(src, []byte("x"), 0o644)
	mt := time.Date(2020, 1, 2, 3, 4, 5, 0, time.UTC)
	os.Chtimes(src, mt, mt)
	dst := filepath.Join(d, "b.jpg")
	if err := Move(src, dst, nil); err != nil {
		t.Fatal(err)
	}
	st, _ := os.Stat(dst)
	if !st.ModTime().Equal(mt) {
		t.Fatal(st.ModTime())
	}
	if _, err := os.Stat(src); !os.IsNotExist(err) {
		t.Fatal("source remains")
	}
}

func TestMoveCrossDeviceViaCopy(t *testing.T) {
	rename = func(a, b string) error { return &os.LinkError{Op: "rename", Old: a, New: b, Err: syscall.EXDEV} }
	defer func() { rename = os.Rename }()
	d := t.TempDir()
	src := filepath.Join(d, "src.jpg")
	os.WriteFile(src, []byte("payload"), 0o644)
	mt := time.Date(2019, 5, 6, 7, 8, 9, 0, time.UTC)
	os.Chtimes(src, mt, mt)
	os.Mkdir(filepath.Join(d, "out"), 0o755)
	dst := filepath.Join(d, "out", "dst.jpg")
	if err := Move(src, dst, nil); err != nil {
		t.Fatal(err)
	}
	b, _ := os.ReadFile(dst)
	st, _ := os.Stat(dst)
	if string(b) != "payload" || !st.ModTime().Equal(mt) {
		t.Fatalf("%q %v", b, st.ModTime())
	}
	if _, err := os.Stat(src); !os.IsNotExist(err) {
		t.Fatal("source remains")
	}
	ents, _ := os.ReadDir(filepath.Join(d, "out"))
	if len(ents) != 1 {
		t.Fatalf("leftover temp files: %v", ents)
	}
}

func TestMoveRefusesOverwrite(t *testing.T) {
	d := t.TempDir()
	a, b := filepath.Join(d, "a"), filepath.Join(d, "b")
	os.WriteFile(a, []byte("a"), 0o644)
	os.WriteFile(b, []byte("b"), 0o644)
	if err := Move(a, b, nil); err != ErrExists {
		t.Fatal(err)
	}
}

func TestMkdirAll(t *testing.T) {
	d := t.TempDir()
	target := filepath.Join(d, "2026", "2026-11", "2026-11-07")
	if err := MkdirAll(target, d, nil); err != nil {
		t.Fatal(err)
	}
	if st, err := os.Stat(target); err != nil || !st.IsDir() {
		t.Fatal(err)
	}
}
