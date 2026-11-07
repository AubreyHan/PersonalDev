// Package walk implements directory traversal with the skip rules and the
// supported-extension list shared by the indexer and the importer (PRD §3).
package walk

import (
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Kind classifies a file by extension.
type Kind int

const (
	KindNone Kind = iota
	KindImage
	KindRaw
	KindVideo
)

var extKinds = map[string]Kind{
	"jpg": KindImage, "jpeg": KindImage, "heic": KindImage, "heif": KindImage,
	"png": KindImage, "gif": KindImage, "webp": KindImage, "tif": KindImage, "tiff": KindImage,
	"cr2": KindRaw, "cr3": KindRaw, "nef": KindRaw, "arw": KindRaw, "dng": KindRaw,
	"raf": KindRaw, "orf": KindRaw, "rw2": KindRaw,
	"mp4": KindVideo, "mov": KindVideo,
}

// KindOf returns the kind of a file name (extension match is case-insensitive).
func KindOf(name string) Kind {
	ext := strings.TrimPrefix(filepath.Ext(name), ".")
	return extKinds[strings.ToLower(ext)]
}

// Supported reports whether a file name has a supported extension.
func Supported(name string) bool { return KindOf(name) != KindNone }

// Skipped reports whether a directory entry name must be ignored: every name
// starting with "@" or "." (QTS system folders, macOS metadata, our own data).
func Skipped(name string) bool {
	return strings.HasPrefix(name, "@") || strings.HasPrefix(name, ".")
}

// Entry is a regular file found during a walk.
type Entry struct {
	Rel     string // slash-separated path relative to the walk root
	Abs     string
	Size    int64
	ModTime time.Time
}

// Options controls a walk.
type Options struct {
	// ExcludeTop lists top-level directory names (directly under root) to skip.
	ExcludeTop []string
	// AllFiles includes files with unsupported extensions as well.
	AllFiles bool
	// OnSkipped, when set, is called for every skipped file (not directory).
	OnSkipped func(abs string)
}

// Walk returns all non-skipped regular files under root, sorted by Rel.
// Unreadable sub-directories are reported in errs but do not stop the walk;
// a failure to read root itself is returned as err.
func Walk(root string, opt Options) (entries []Entry, errs []error, err error) {
	if _, err := os.Stat(root); err != nil {
		return nil, nil, err
	}
	exclude := map[string]bool{}
	for _, n := range opt.ExcludeTop {
		exclude[n] = true
	}
	werr := filepath.WalkDir(root, func(p string, d fs.DirEntry, e error) error {
		if e != nil {
			if p == root {
				return e
			}
			errs = append(errs, e)
			if d != nil && d.IsDir() {
				return fs.SkipDir
			}
			return nil
		}
		if p == root {
			return nil
		}
		name := d.Name()
		if d.IsDir() {
			if Skipped(name) {
				return fs.SkipDir
			}
			if filepath.Dir(p) == filepath.Clean(root) && exclude[name] {
				return fs.SkipDir
			}
			return nil
		}
		if Skipped(name) {
			if opt.OnSkipped != nil && d.Type().IsRegular() {
				opt.OnSkipped(p)
			}
			return nil
		}
		if !d.Type().IsRegular() {
			return nil
		}
		if !opt.AllFiles && !Supported(name) {
			return nil
		}
		info, e := d.Info()
		if e != nil {
			if !errors.Is(e, fs.ErrNotExist) {
				errs = append(errs, e)
			}
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		entries = append(entries, Entry{
			Rel:     filepath.ToSlash(rel),
			Abs:     p,
			Size:    info.Size(),
			ModTime: info.ModTime(),
		})
		return nil
	})
	if werr != nil {
		return nil, errs, werr
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].Rel < entries[j].Rel })
	return entries, errs, nil
}
