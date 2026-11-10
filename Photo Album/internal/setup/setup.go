// Package setup validates the first-run configuration (PRD §2).
package setup

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"photo-album/internal/fsbrowse"
	"photo-album/internal/scheduler"
)

// Request is the first-run form.
type Request struct {
	AlbumDir        string `json:"album_dir"`
	ImportDir       string `json:"import_dir"`
	WatchDir        string `json:"watch_dir,omitempty"`
	IntervalMinutes int64  `json:"interval_minutes"`
}

// ValidationError lists every problem found.
type ValidationError struct {
	Messages []string `json:"messages"`
}

func (e *ValidationError) Error() string { return strings.Join(e.Messages, "；") }

// Validate checks the request and returns cleaned directory paths.
func Validate(shareRoot string, r Request) (album, imp, watch string, err error) {
	var msgs []string
	album, aerr := checkDir(shareRoot, "相册目录", r.AlbumDir)
	imp, ierr := checkDir(shareRoot, "临时导入目录", r.ImportDir)
	if aerr != "" {
		msgs = append(msgs, aerr)
	}
	if ierr != "" {
		msgs = append(msgs, ierr)
	}
	if aerr == "" && ierr == "" {
		ra, e1 := filepath.EvalSymlinks(album)
		ri, e2 := filepath.EvalSymlinks(imp)
		switch {
		case e1 != nil || e2 != nil:
			msgs = append(msgs, "无法解析目录的真实路径")
		case ra == ri:
			msgs = append(msgs, "相册目录和临时导入目录不能相同")
		case fsbrowse.Within(ra, ri) || fsbrowse.Within(ri, ra):
			msgs = append(msgs, "相册目录和临时导入目录不能互相包含")
		}
		if m := probeWritable("相册目录", album); m != "" {
			msgs = append(msgs, m)
		}
		if m := probeWritable("临时导入目录", imp); m != "" {
			msgs = append(msgs, m)
		}
		if strings.TrimSpace(r.WatchDir) != "" {
			w, werr := ValidateWatchDir(shareRoot, album, imp, r.WatchDir)
			if werr != nil {
				var ve *ValidationError
				if errors.As(werr, &ve) {
					msgs = append(msgs, ve.Messages...)
				} else {
					msgs = append(msgs, werr.Error())
				}
			} else {
				watch = w
			}
		}
	}
	if !scheduler.ValidInterval(r.IntervalMinutes) {
		msgs = append(msgs, fmt.Sprintf("扫描间隔必须是 %d–%d 之间的整数（分钟）", scheduler.MinInterval, scheduler.MaxInterval))
	}
	if len(msgs) > 0 {
		return "", "", "", &ValidationError{Messages: msgs}
	}
	return album, imp, watch, nil
}

// ValidateWatchDir checks an optional copy-watched directory against shareRoot,
// albumDir, and importDir. An empty watchDir is valid (disables the watch dir).
func ValidateWatchDir(shareRoot, albumDir, importDir, watchDir string) (string, error) {
	if strings.TrimSpace(watchDir) == "" {
		return "", nil
	}
	var msgs []string
	watch, werr := checkDir(shareRoot, "拷贝监控目录", watchDir)
	if werr != "" {
		return "", &ValidationError{Messages: []string{werr}}
	}
	rw, ew := filepath.EvalSymlinks(watch)
	ra, ea := filepath.EvalSymlinks(albumDir)
	ri, ei := filepath.EvalSymlinks(importDir)
	if ew != nil || ea != nil || ei != nil {
		msgs = append(msgs, "无法解析目录的真实路径")
	} else {
		if rw == ra {
			msgs = append(msgs, "拷贝监控目录和相册目录不能相同")
		} else if fsbrowse.Within(rw, ra) || fsbrowse.Within(ra, rw) {
			msgs = append(msgs, "拷贝监控目录和相册目录不能互相包含")
		}
		if rw == ri {
			msgs = append(msgs, "拷贝监控目录和临时导入目录不能相同")
		} else if fsbrowse.Within(rw, ri) || fsbrowse.Within(ri, rw) {
			msgs = append(msgs, "拷贝监控目录和临时导入目录不能互相包含")
		}
	}
	if _, err := os.ReadDir(watch); err != nil {
		msgs = append(msgs, "拷贝监控目录没有读取权限")
	}
	if len(msgs) > 0 {
		return "", &ValidationError{Messages: msgs}
	}
	return watch, nil
}

func checkDir(root, label, p string) (string, string) {
	if strings.TrimSpace(p) == "" {
		return "", label + "未选择"
	}
	if !filepath.IsAbs(p) {
		return "", label + "必须是绝对路径"
	}
	p = filepath.Clean(p)
	if !fsbrowse.Within(root, p) || p == filepath.Clean(root) {
		return "", label + "必须位于共享文件夹（" + root + "）之下"
	}
	st, err := os.Stat(p)
	if err != nil || !st.IsDir() {
		return "", label + "不存在或不是目录"
	}
	return p, ""
}

func probeWritable(label, dir string) string {
	var b [6]byte
	_, _ = rand.Read(b[:])
	p := filepath.Join(dir, ".photo-album-probe-"+hex.EncodeToString(b[:]))
	f, err := os.OpenFile(p, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o644)
	if err != nil {
		return label + "没有读写权限"
	}
	f.Close()
	if err := os.Remove(p); err != nil {
		return label + "没有删除权限"
	}
	if _, err := os.ReadDir(dir); err != nil {
		return label + "没有读取权限"
	}
	return ""
}
