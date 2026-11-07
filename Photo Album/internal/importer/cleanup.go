package importer

import (
	"os"
	"path/filepath"
	"strings"
)

// junkFile reports whether a file is system clutter that does not keep a
// directory "non-empty" (PRD §3 空目录处理, ARCHITECTURE §5.9).
func junkFile(name string) bool {
	return name == ".DS_Store" || strings.HasPrefix(name, "._") ||
		strings.EqualFold(name, "Thumbs.db") || strings.EqualFold(name, "desktop.ini")
}

// junkDir reports whether a directory is QTS thumbnail clutter.
func junkDir(name string) bool { return name == ".@__thumb" || name == "@__thumb" }

// junkOnly reports whether dir contains nothing but junk.
func junkOnly(dir string) (bool, []os.DirEntry) {
	ents, err := os.ReadDir(dir)
	if err != nil {
		return false, nil
	}
	for _, e := range ents {
		if e.IsDir() {
			if !junkDir(e.Name()) {
				return false, nil
			}
		} else if !junkFile(e.Name()) {
			return false, nil
		}
	}
	return true, ents
}

// cleanupDir immediately removes dir if it has become empty (or contains only
// system junk), walking up towards (but never removing) root.
func cleanupDir(root, dir string) {
	root = filepath.Clean(root)
	d := filepath.Clean(dir)
	for d != root && strings.HasPrefix(d, root+string(filepath.Separator)) {
		ok, ents := junkOnly(d)
		if !ok {
			break
		}
		for _, e := range ents {
			_ = os.RemoveAll(filepath.Join(d, e.Name()))
		}
		if err := os.Remove(d); err != nil {
			break
		}
		d = filepath.Dir(d)
	}
}
