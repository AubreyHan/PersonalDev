package importer

import (
	"context"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
	"time"

	"photo-album/internal/indexer"
	"photo-album/internal/metadata"
	"photo-album/internal/store"
	"photo-album/internal/walk"
)

var cst = time.FixedZone("CST", 8*3600)

func day(d int) time.Time { return time.Date(2026, 11, d, 12, 0, 0, 0, cst) }

func put(t *testing.T, root, rel, content string) {
	t.Helper()
	p := filepath.Join(root, filepath.FromSlash(rel))
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}

func tree(t *testing.T, root string) []string {
	t.Helper()
	var out []string
	filepath.Walk(root, func(p string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(root, p)
		rel = filepath.ToSlash(rel)
		if rel == "." || strings.HasPrefix(rel, ".photo-album") || strings.HasPrefix(rel, "logs") {
			return nil
		}
		if info.IsDir() {
			rel += "/"
		}
		out = append(out, rel)
		return nil
	})
	sort.Strings(out)
	return out
}

type env struct {
	album, inbox string
	st           *store.Store
	im           *Importer
	meta         map[string]metadata.Info
}

func newEnv(t *testing.T) *env {
	t.Helper()
	e := &env{album: t.TempDir(), inbox: t.TempDir(), meta: map[string]metadata.Info{}}
	st, err := store.Open(e.album, true)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { st.Close() })
	e.st = st
	e.im = New(st, indexer.New(st, e.album), e.album, e.inbox, cst)
	e.im.WriteWait = 0
	e.im.Now = func() time.Time { return time.Date(2026, 11, 7, 10, 0, 30, 0, cst) }
	e.im.ReadMeta = func(p string, _ *time.Location) (metadata.Info, error) {
		rel, _ := filepath.Rel(e.inbox, p)
		return e.meta[filepath.ToSlash(rel)], nil
	}
	return e
}

func (e *env) in(t *testing.T, rel, content string, taken time.Time, cid string) {
	put(t, e.inbox, rel, content)
	e.meta[rel] = metadata.Info{Taken: taken, ContentID: cid}
}

func TestFullScenario(t *testing.T) {
	e := newEnv(t)
	// Existing album content.
	put(t, e.album, "2026/2026-11/2026-11-07/IMG_0001.JPG", "same1")
	put(t, e.album, "2026/2026-11/2026-11-07/IMG_0002.JPG", "old2")
	put(t, e.album, "2026/2026-11/2026-11-07/img_0002_1.jpg", "old2b")
	put(t, e.album, "2025/2025-01/2025-01-01/IMG_9999.JPG", "dupcontent")
	put(t, e.album, "2026/2026-11/2026-11-05/IMG_0006.MOV", "othermov")
	if err := e.im.Indexer.Initial(context.Background()); err != nil {
		t.Fatal(err)
	}

	// Import directory content.
	e.in(t, "a/IMG_0001.JPG", "same1", day(7), "")               // same name, same content
	e.in(t, "IMG_0002.jpg", "new2", day(7), "")                  // same name (case), different content
	e.in(t, "copy.jpg", "dupcontent", day(7), "")                // content dup elsewhere
	e.in(t, "b/IMG_0003.JPG", "three", day(7), "")               // first of batch dup
	e.in(t, "c/IMG_0003.JPG", "three", day(7), "")               // batch dup
	e.in(t, "live/IMG_0004.HEIC", "heic4", day(6), "CID-AAAA-4") // live photo (MOV dated next day)
	e.in(t, "live/IMG_0004.MOV", "mov4", day(7), "CID-AAAA-4")
	e.in(t, "lp/IMG_0005.HEIC", "heic5", day(7), "") // live partial dup (paired by stem)
	e.in(t, "lp/IMG_0005.MOV", "dupcontent", day(7), "")
	e.in(t, "lr/IMG_0006.HEIC", "h6", day(5), "CID-BBBB-6") // live rename (MOV name taken)
	e.in(t, "lr/IMG_0006.mov", "m6", day(5), "CID-BBBB-6")
	put(t, e.inbox, "d/x.jpg", "x") // no metadata -> mtime
	mt := time.Date(2024, 2, 29, 9, 0, 0, 0, cst)
	os.Chtimes(filepath.Join(e.inbox, "d/x.jpg"), mt, mt)
	put(t, e.inbox, "d/.DS_Store", "junk")
	put(t, e.inbox, "d/.@__thumb/x.jpg", "thumb")
	put(t, e.inbox, "notes.txt", "keep")
	os.MkdirAll(filepath.Join(e.inbox, "empty"), 0o755)

	start := time.Date(2026, 11, 7, 10, 0, 0, 0, cst)
	res, err := e.im.Run(context.Background(), start)
	if err != nil {
		t.Fatal(err)
	}
	if res.Skipped || res.Aborted != "" || res.Failed != 0 {
		t.Fatalf("%+v", res)
	}

	wantAlbum := []string{
		"2024/", "2024/2024-02/", "2024/2024-02/2024-02-29/", "2024/2024-02/2024-02-29/x.jpg",
		"2025/", "2025/2025-01/", "2025/2025-01/2025-01-01/", "2025/2025-01/2025-01-01/IMG_9999.JPG",
		"2026/", "2026/2026-11/",
		"2026/2026-11/2026-11-05/", "2026/2026-11/2026-11-05/IMG_0006.MOV",
		"2026/2026-11/2026-11-05/IMG_0006_1.HEIC", "2026/2026-11/2026-11-05/IMG_0006_1.mov",
		"2026/2026-11/2026-11-06/", "2026/2026-11/2026-11-06/IMG_0004.HEIC", "2026/2026-11/2026-11-06/IMG_0004.MOV",
		"2026/2026-11/2026-11-07/", "2026/2026-11/2026-11-07/IMG_0001.JPG", "2026/2026-11/2026-11-07/IMG_0002.JPG",
		"2026/2026-11/2026-11-07/IMG_0002_2.jpg", "2026/2026-11/2026-11-07/IMG_0003.JPG",
		"2026/2026-11/2026-11-07/img_0002_1.jpg",
	}
	if got := tree(t, e.album); strings.Join(got, "\n") != strings.Join(wantAlbum, "\n") {
		t.Fatalf("album:\n%s", strings.Join(got, "\n"))
	}

	d := "duplicates/20261107_100000/"
	wantInbox := []string{
		"duplicates/", d[:len(d)-1] + "/",
		d + "a/", d + "a/IMG_0001.JPG",
		d + "c/", d + "c/IMG_0003.JPG",
		d + "copy.jpg",
		d + "lp/", d + "lp/IMG_0005.HEIC", d + "lp/IMG_0005.MOV",
		"empty/", "notes.txt",
	}
	if got := tree(t, e.inbox); strings.Join(got, "\n") != strings.Join(wantInbox, "\n") {
		t.Fatalf("inbox:\n%s", strings.Join(got, "\n"))
	}

	logb, err := os.ReadFile(filepath.Join(e.inbox, "logs", "20261107.log"))
	if err != nil {
		t.Fatal(err)
	}
	A := func(rel string) string { return filepath.Join(e.album, rel) }
	I := func(rel string) string { return filepath.Join(e.inbox, rel) }
	wantLog := strings.Join([]string{
		"[2026-11-07 10:00:00] 扫描开始 2026-11-07 10:00:00 | 完成 2026-11-07 10:00:30",
		"同名改名 | " + I("IMG_0002.jpg") + " | " + A("2026/2026-11/2026-11-07/IMG_0002_2.jpg"),
		"同名重复 | " + I("a/IMG_0001.JPG") + " | " + A("2026/2026-11/2026-11-07/IMG_0001.JPG"),
		"本次重复 | " + I("c/IMG_0003.JPG") + " | " + A("2026/2026-11/2026-11-07/IMG_0003.JPG"),
		"内容重复 | " + I("copy.jpg") + " | " + A("2025/2025-01/2025-01-01/IMG_9999.JPG"),
		"Live Photo 部分重复 | " + I("lp/IMG_0005.HEIC") + " | -",
		"Live Photo 部分重复 | " + I("lp/IMG_0005.MOV") + " | " + A("2025/2025-01/2025-01-01/IMG_9999.JPG"),
		"Live Photo 同名改名 | " + I("lr/IMG_0006.HEIC") + " | " + A("2026/2026-11/2026-11-05/IMG_0006_1.HEIC"),
		"Live Photo 同名改名 | " + I("lr/IMG_0006.mov") + " | " + A("2026/2026-11/2026-11-05/IMG_0006_1.mov"),
	}, "\n") + "\n"
	if string(logb) != wantLog {
		t.Fatalf("log:\n%s\nwant:\n%s", logb, wantLog)
	}

	// Imported files are indexed.
	if rel, ok, _ := e.st.FindByMD5(md5s(t, A("2026/2026-11/2026-11-06/IMG_0004.MOV"))); !ok || rel != "2026/2026-11/2026-11-06/IMG_0004.MOV" {
		t.Fatal(rel, ok)
	}
}

func md5s(t *testing.T, p string) string {
	t.Helper()
	s, err := New(nil, nil, "", "", cst).Hash(p)
	if err != nil {
		t.Fatal(err)
	}
	return s
}

func TestWriteActivitySkipsScan(t *testing.T) {
	e := newEnv(t)
	e.im.Indexer.Initial(context.Background())
	e.in(t, "a.jpg", "a", day(7), "")
	e.im.Sleep = func(ctx context.Context, d time.Duration) error {
		put(t, e.inbox, "a.jpg", "a-grown") // file still being written
		return nil
	}
	start := time.Date(2026, 11, 7, 11, 0, 0, 0, cst)
	res, err := e.im.Run(context.Background(), start)
	if err != nil || !res.Skipped {
		t.Fatal(res, err)
	}
	if _, err := os.Stat(filepath.Join(e.inbox, "a.jpg")); err != nil {
		t.Fatal("file should stay")
	}
	b, _ := os.ReadFile(filepath.Join(e.inbox, "logs", "20261107.log"))
	if string(b) != "[2026-11-07 11:00:00] 跳过 | 检测到写入，跳过本次扫描\n" {
		t.Fatalf("%q", b)
	}
}

func TestFailureKeepsFileAndContinues(t *testing.T) {
	e := newEnv(t)
	e.im.Indexer.Initial(context.Background())
	e.in(t, "bad.jpg", "bad", day(7), "")
	e.in(t, "good.jpg", "good", day(7), "")
	orig := e.im.Hash
	e.im.Hash = func(p string) (string, error) {
		if strings.HasSuffix(p, "bad.jpg") {
			return "", os.ErrPermission
		}
		return orig(p)
	}
	res, err := e.im.Run(context.Background(), time.Date(2026, 11, 7, 12, 0, 0, 0, cst))
	if err != nil || res.Failed != 1 || res.Imported != 1 {
		t.Fatal(res, err)
	}
	if _, err := os.Stat(filepath.Join(e.inbox, "bad.jpg")); err != nil {
		t.Fatal("failed file must stay")
	}
	b, _ := os.ReadFile(filepath.Join(e.inbox, "logs", "20261107.log"))
	if !strings.Contains(string(b), "失败 | "+filepath.Join(e.inbox, "bad.jpg")+" | permission denied") {
		t.Fatalf("%s", b)
	}
}

func entry(rel string) walk.Entry { return walk.Entry{Rel: rel} }

func TestBuildUnitsAmbiguousStem(t *testing.T) {
	mk := func(rel, cid string) *file {
		return &file{Info: metadata.Info{ContentID: cid}, Entry: entry(rel)}
	}
	files := []*file{
		mk("x/IMG_1.HEIC", ""), mk("x/IMG_1.JPG", ""), mk("x/IMG_1.MOV", ""),
		mk("y/A.heic", "C1"), mk("y/B.mov", "C1"), // paired by CID despite different names
		mk("z/P.jpg", "C2"), mk("z/P.mov", "C3"), // conflicting CIDs: not paired
	}
	units := buildUnits(files)
	var got []string
	for _, u := range units {
		var names []string
		for _, f := range u.files {
			names = append(names, f.Rel)
		}
		got = append(got, strings.Join(names, "+"))
	}
	want := "x/IMG_1.HEIC|x/IMG_1.JPG|x/IMG_1.MOV|y/A.heic+y/B.mov|z/P.jpg|z/P.mov"
	if strings.Join(got, "|") != want {
		t.Fatal(strings.Join(got, "|"))
	}
}

func TestImmediateDirCleanupAndM4V(t *testing.T) {
	e := newEnv(t)
	e.im.Indexer.Initial(context.Background())
	e.in(t, "dir1/sub/1.jpg", "one", day(7), "")
	put(t, e.inbox, "dir1/sub/.DS_Store", "junk")
	e.in(t, "dir2/2.m4v", "two-m4v", day(7), "")

	origHash := e.im.Hash
	dir1GoneBeforeDir2 := false
	e.im.Hash = func(p string) (string, error) {
		if strings.HasSuffix(p, "2.m4v") {
			// By the time dir2/2.m4v is hashed, dir1 and dir1/sub must already be deleted.
			if _, err := os.Stat(filepath.Join(e.inbox, "dir1")); os.IsNotExist(err) {
				dir1GoneBeforeDir2 = true
			}
		}
		return origHash(p)
	}

	res, err := e.im.Run(context.Background(), time.Date(2026, 11, 7, 13, 0, 0, 0, cst))
	if err != nil || res.Imported != 2 {
		t.Fatalf("res=%+v err=%v", res, err)
	}
	if !dir1GoneBeforeDir2 {
		t.Fatal("expected dir1 to be deleted immediately after its last file was moved, before dir2 was processed")
	}
	if _, err := os.Stat(filepath.Join(e.album, "2026/2026-11/2026-11-07/2.m4v")); err != nil {
		t.Fatalf("m4v not imported: %v", err)
	}
}
