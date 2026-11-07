// Package fsbrowse lists directories under the NAS share root for the
// first-run directory picker (ARCHITECTURE §5.1).
package fsbrowse

import (
	"errors"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"photo-album/internal/walk"
)

// ErrOutside is returned for paths outside the share root.
var ErrOutside = errors.New("路径不在共享文件夹范围内")

// Dir is one listed directory.
type Dir struct {
	Name string `json:"name"`
	Path string `json:"path"`
}

// Browser lists directories below Root (normally /share).
type Browser struct {
	Root string
}

// Within reports whether p (cleaned, absolute) is Root or below it.
func Within(root, p string) bool {
	root, p = filepath.Clean(root), filepath.Clean(p)
	return p == root || strings.HasPrefix(p, root+string(filepath.Separator))
}

// List returns the sub-directories of p ("" means Root), skipping "@"/"."
// entries and, at the root level, raw volume mounts such as CACHEDEV1_DATA.
func (b *Browser) List(p string) ([]Dir, error) {
	if p == "" {
		p = b.Root
	}
	if !filepath.IsAbs(p) {
		return nil, ErrOutside
	}
	p = filepath.Clean(p)
	if !Within(b.Root, p) {
		return nil, ErrOutside
	}
	ents, err := os.ReadDir(p)
	if err != nil {
		return nil, err
	}
	atRoot := p == filepath.Clean(b.Root)
	out := []Dir{}
	for _, e := range ents {
		name := e.Name()
		if walk.Skipped(name) {
			continue
		}
		if atRoot && strings.HasSuffix(name, "_DATA") {
			continue
		}
		full := filepath.Join(p, name)
		st, err := os.Stat(full) // follows share symlinks
		if err != nil || !st.IsDir() {
			continue
		}
		out = append(out, Dir{Name: name, Path: full})
	}
	sort.Slice(out, func(i, j int) bool { return strings.ToLower(out[i].Name) < strings.ToLower(out[j].Name) })
	return out, nil
}
