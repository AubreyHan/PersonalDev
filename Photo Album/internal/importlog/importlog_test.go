package importlog

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

var cst = time.FixedZone("CST", 8*3600)

func TestWriteScanAcrossMidnightUsesStartDate(t *testing.T) {
	d := t.TempDir()
	l := New(d, cst)
	start := time.Date(2026, 11, 7, 23, 59, 0, 0, cst)
	end := start.Add(3 * time.Minute)
	err := l.WriteScan(start, end, "", []Entry{
		{Type: TypeRenamed, Source: "/in/a.jpg", Ref: "/album/2026/2026-11/2026-11-07/a_1.jpg"},
		{Type: LivePrefix + TypeContentDup, Source: "/in/b.MOV", Ref: "/album/x/b.MOV"},
		{Type: TypeLivePartial, Source: "/in/b.HEIC"},
	})
	if err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(filepath.Join(d, "logs", "20261107.log"))
	if err != nil {
		t.Fatal(err)
	}
	want := strings.Join([]string{
		"[2026-11-07 23:59:00] 扫描开始 2026-11-07 23:59:00 | 完成 2026-11-08 00:02:00",
		"同名改名 | /in/a.jpg | /album/2026/2026-11/2026-11-07/a_1.jpg",
		"Live Photo 内容重复 | /in/b.MOV | /album/x/b.MOV",
		"Live Photo 部分重复 | /in/b.HEIC | -",
	}, "\n") + "\n"
	if string(b) != want {
		t.Fatalf("got:\n%s\nwant:\n%s", b, want)
	}
	if err := l.WriteSkip(end, ReasonBusy); err != nil {
		t.Fatal(err)
	}
	b, _ = os.ReadFile(filepath.Join(d, "logs", "20261108.log"))
	if string(b) != "[2026-11-08 00:02:00] 跳过 | 上次扫描未完成，跳过\n" {
		t.Fatalf("%q", b)
	}
}

func TestPrune(t *testing.T) {
	d := t.TempDir()
	l := New(d, cst)
	os.MkdirAll(l.Dir, 0o755)
	for _, n := range []string{"20260809.log", "20260808.log", "20261107.log", "notes.txt", "2026.log"} {
		os.WriteFile(filepath.Join(l.Dir, n), nil, 0o644)
	}
	now := time.Date(2026, 11, 7, 10, 0, 0, 0, cst) // cutoff = 2026-08-09
	if err := l.Prune(now); err != nil {
		t.Fatal(err)
	}
	ents, _ := os.ReadDir(l.Dir)
	var names []string
	for _, e := range ents {
		names = append(names, e.Name())
	}
	got := strings.Join(names, ",")
	if got != "2026.log,20260809.log,20261107.log,notes.txt" {
		t.Fatal(got)
	}
}
