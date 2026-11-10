package setup

import (
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"photo-album/internal/fsbrowse"
)

func mk(t *testing.T, root string, rels ...string) {
	for _, r := range rels {
		if err := os.MkdirAll(filepath.Join(root, r), 0o755); err != nil {
			t.Fatal(err)
		}
	}
}

func msgs(err error) string {
	var ve *ValidationError
	if errors.As(err, &ve) {
		return strings.Join(ve.Messages, "|")
	}
	if err != nil {
		return err.Error()
	}
	return ""
}

func TestValidate(t *testing.T) {
	root, _ := filepath.EvalSymlinks(t.TempDir())
	mk(t, root, "CACHEDEV1_DATA/Photos/sub", "CACHEDEV1_DATA/Inbox", ".hidden", "@Recycle")
	os.Symlink(filepath.Join(root, "CACHEDEV1_DATA/Photos"), filepath.Join(root, "Photos"))
	os.Symlink(filepath.Join(root, "CACHEDEV1_DATA/Inbox"), filepath.Join(root, "Inbox"))
	P := func(r string) string { return filepath.Join(root, r) }

	if _, _, _, err := Validate(root, Request{AlbumDir: P("Photos"), ImportDir: P("Inbox"), IntervalMinutes: 60}); err != nil {
		t.Fatal(err)
	}
	cases := map[string]Request{
		"不能相同":            {AlbumDir: P("Photos"), ImportDir: P("CACHEDEV1_DATA/Photos"), IntervalMinutes: 60},
		"不能互相包含":          {AlbumDir: P("Photos"), ImportDir: P("CACHEDEV1_DATA/Photos/sub"), IntervalMinutes: 60},
		"拷贝监控目录和相册目录不能相同": {AlbumDir: P("Photos"), ImportDir: P("Inbox"), WatchDir: P("CACHEDEV1_DATA/Photos"), IntervalMinutes: 60},
		"拷贝监控目录和临时导入目录不能相同": {AlbumDir: P("Photos"), ImportDir: P("Inbox"), WatchDir: P("Inbox"), IntervalMinutes: 60},
		"扫描间隔":            {AlbumDir: P("Photos"), ImportDir: P("Inbox"), IntervalMinutes: 4},
		"不存在":             {AlbumDir: P("Photos"), ImportDir: P("nope"), IntervalMinutes: 60},
		"共享文件夹":           {AlbumDir: P("Photos"), ImportDir: "/tmp", IntervalMinutes: 60},
		"临时导入目录未选择":       {AlbumDir: P("Photos"), ImportDir: "", IntervalMinutes: 1441},
	}
	for want, req := range cases {
		_, _, _, err := Validate(root, req)
		if !strings.Contains(msgs(err), want) {
			t.Errorf("%s: got %q", want, msgs(err))
		}
	}
	if runtime.GOOS != "windows" && os.Getuid() != 0 {
		ro := P("CACHEDEV1_DATA/ro")
		mk(t, root, "CACHEDEV1_DATA/ro")
		os.Chmod(ro, 0o555)
		defer os.Chmod(ro, 0o755)
		if _, _, _, err := Validate(root, Request{AlbumDir: P("Photos"), ImportDir: ro, IntervalMinutes: 60}); !strings.Contains(msgs(err), "没有读写权限") {
			t.Errorf("read-only import: %q", msgs(err))
		}
		// Read-only watch directory IS allowed since files are only copied out of it.
		if _, _, w, err := Validate(root, Request{AlbumDir: P("Photos"), ImportDir: P("Inbox"), WatchDir: ro, IntervalMinutes: 60}); err != nil || w != ro {
			t.Errorf("read-only watch should succeed: w=%q err=%v", w, err)
		}
	}

	b := &fsbrowse.Browser{Root: root}
	dirs, err := b.List("")
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, d := range dirs {
		names = append(names, d.Name)
	}
	if strings.Join(names, ",") != "Inbox,Photos" {
		t.Fatal(names)
	}
	if _, err := b.List(P("../..")); err != fsbrowse.ErrOutside {
		t.Fatal(err)
	}
	if sub, _ := b.List(P("Photos")); len(sub) != 1 || sub[0].Name != "sub" {
		t.Fatal(sub)
	}
}
