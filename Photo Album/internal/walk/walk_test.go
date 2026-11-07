package walk

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func touch(t *testing.T, root, rel string) {
	t.Helper()
	p := filepath.Join(root, filepath.FromSlash(rel))
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, []byte(rel), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestKindOf(t *testing.T) {
	cases := map[string]Kind{
		"a.JPG": KindImage, "a.HeIc": KindImage, "b.CR3": KindRaw, "c.rw2": KindRaw,
		"d.MOV": KindVideo, "e.mp4": KindVideo, "f.txt": KindNone, "noext": KindNone,
		"g.tiff": KindImage, "h.webp": KindImage,
	}
	for name, want := range cases {
		if got := KindOf(name); got != want {
			t.Errorf("KindOf(%q)=%v want %v", name, got, want)
		}
	}
}

func TestWalkSkipRules(t *testing.T) {
	root := t.TempDir()
	for _, rel := range []string{
		"a.jpg", "B/IMG.JPG", "B/notes.txt",
		"@Recycle/x.jpg", "B/.@__thumb/t.jpg", "B/._IMG.JPG", ".DS_Store",
		"logs/20260101.log", "logs/x.jpg", "duplicates/20260101_000000/a.jpg",
		"C/logs/y.jpg", ".photo-album/db.jpg",
	} {
		touch(t, root, rel)
	}
	var skipped []string
	got, errs, err := Walk(root, Options{
		ExcludeTop: []string{"logs", "duplicates"},
		OnSkipped:  func(abs string) { r, _ := filepath.Rel(root, abs); skipped = append(skipped, filepath.ToSlash(r)) },
	})
	if err != nil || len(errs) > 0 {
		t.Fatal(err, errs)
	}
	var rels []string
	for _, e := range got {
		rels = append(rels, e.Rel)
	}
	want := []string{"B/IMG.JPG", "C/logs/y.jpg", "a.jpg"}
	if !reflect.DeepEqual(rels, want) {
		t.Fatalf("got %v want %v", rels, want)
	}
	if len(skipped) != 2 { // .DS_Store, B/._IMG.JPG (files in skipped dirs are not visited)
		t.Fatalf("skipped=%v", skipped)
	}

	all, _, _ := Walk(root, Options{ExcludeTop: []string{"logs", "duplicates"}, AllFiles: true})
	if len(all) != 4 {
		t.Fatalf("AllFiles got %d entries", len(all))
	}
}
