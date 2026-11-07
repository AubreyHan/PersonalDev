// Package perm makes imported files and new date directories inherit the
// owner and mode of the album directory (PRD §3 文件权限).
package perm

import (
	"fmt"
	"log"
	"os"
	"syscall"
)

// Spec is the ownership/mode to apply.
type Spec struct {
	UID, GID int
	DirMode  os.FileMode
	FileMode os.FileMode
}

// FromDir derives a Spec from a directory: same owner, same mode for
// directories, and the same mode minus execute bits for files.
func FromDir(dir string) (*Spec, error) {
	st, err := os.Stat(dir)
	if err != nil {
		return nil, err
	}
	sys, ok := st.Sys().(*syscall.Stat_t)
	if !ok {
		return nil, fmt.Errorf("perm: unsupported stat for %s", dir)
	}
	mode := st.Mode().Perm()
	return &Spec{
		UID:      int(sys.Uid),
		GID:      int(sys.Gid),
		DirMode:  mode,
		FileMode: mode &^ 0o111,
	}, nil
}

// ApplyFile sets owner and mode on a file. Ownership errors are logged but
// not fatal (the QPKG runs as admin on the NAS; in development it may not).
func (s *Spec) ApplyFile(path string) error {
	if s == nil {
		return nil
	}
	s.chown(path)
	return os.Chmod(path, s.FileMode)
}

// ApplyDir sets owner and mode on a directory.
func (s *Spec) ApplyDir(path string) error {
	if s == nil {
		return nil
	}
	s.chown(path)
	return os.Chmod(path, s.DirMode)
}

func (s *Spec) chown(path string) {
	if err := os.Lchown(path, s.UID, s.GID); err != nil {
		log.Printf("perm: chown %s: %v", path, err)
	}
}
