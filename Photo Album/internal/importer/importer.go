// Package importer implements one scan of the import directory
// (PRD §3, ARCHITECTURE §5.4 and §5.7).
package importer

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"
	"time"

	"photo-album/internal/hashing"
	"photo-album/internal/importlog"
	"photo-album/internal/indexer"
	"photo-album/internal/metadata"
	"photo-album/internal/mover"
	"photo-album/internal/perm"
	"photo-album/internal/store"
	"photo-album/internal/walk"
)

// DuplicatesDir is the duplicates directory inside the import directory.
const DuplicatesDir = "duplicates"

// ExcludeTop lists import-directory sub-directories that are never scanned.
var ExcludeTop = []string{importlog.DirName, DuplicatesDir}

const abortDiskFull = "磁盘空间不足"

// Importer runs scans. All fields except the function hooks are required.
type Importer struct {
	Store     *store.Store
	Indexer   *indexer.Indexer
	Log       *importlog.Logger
	AlbumDir  string
	ImportDir string
	Loc       *time.Location

	// WriteWait is the pause between the two write-detection snapshots.
	WriteWait time.Duration
	// Hooks (defaults are set by New).
	Now      func() time.Time
	Sleep    func(ctx context.Context, d time.Duration) error
	ReadMeta func(path string, loc *time.Location) (metadata.Info, error)
	Hash     func(path string) (string, error)
}

// New returns an Importer with production defaults.
func New(s *store.Store, ix *indexer.Indexer, albumDir, importDir string, loc *time.Location) *Importer {
	return &Importer{
		Store: s, Indexer: ix, AlbumDir: albumDir, ImportDir: importDir, Loc: loc,
		Log:       importlog.New(importDir, loc),
		WriteWait: 30 * time.Second,
		Now:       time.Now,
		Sleep:     sleepCtx,
		ReadMeta:  metadata.Read,
		Hash:      hashing.MD5File,
	}
}

func sleepCtx(ctx context.Context, d time.Duration) error {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-t.C:
		return nil
	}
}

// Result summarises one scan.
type Result struct {
	Skipped    bool // write activity detected
	Aborted    string
	Imported   int
	Duplicates int
	Failed     int
}

// errAbort carries a scan-abort reason.
type errAbort struct{ reason string }

func (e errAbort) Error() string { return e.reason }

// Run performs one scan that started at start.
func (im *Importer) Run(ctx context.Context, start time.Time) (Result, error) {
	var res Result

	// 1. Write detection.
	writing, err := im.writeActivity(ctx)
	if err != nil {
		return res, err
	}
	if writing {
		res.Skipped = true
		return res, im.Log.WriteSkip(start, importlog.ReasonWriting)
	}

	// 2. Bring the album index up to date.
	if err := im.Indexer.Incremental(ctx); err != nil {
		return res, fmt.Errorf("incremental index: %w", err)
	}

	// 3. Collect files and metadata.
	entries, _, err := walk.Walk(im.ImportDir, walk.Options{ExcludeTop: ExcludeTop})
	if err != nil {
		return res, err
	}
	files := make([]*file, 0, len(entries))
	for _, e := range entries {
		info, _ := im.ReadMeta(e.Abs, im.Loc)
		files = append(files, &file{Entry: e, Info: info})
	}

	spec, err := perm.FromDir(im.AlbumDir)
	if err != nil {
		return res, err
	}
	sc := &scan{
		im:      im,
		start:   start,
		spec:    spec,
		batch:   map[string]string{},
		touched: map[string]bool{},
	}

	// 4. Process units.
	for _, u := range buildUnits(files) {
		if err := ctx.Err(); err != nil {
			sc.abort = "已取消"
			break
		}
		if err := sc.process(u); err != nil {
			var ab errAbort
			if errors.As(err, &ab) {
				sc.abort = ab.reason
				break
			}
			sc.abort = err.Error()
			break
		}
	}

	// 5. Clean up, log, prune.
	cleanupDirs(im.ImportDir, sc.touched)
	res.Imported, res.Duplicates, res.Failed, res.Aborted = sc.imported, sc.dups, sc.failed, sc.abort
	if err := im.Log.WriteScan(start, im.Now(), sc.abort, sc.entries); err != nil {
		return res, err
	}
	return res, im.Log.Prune(im.Now())
}

type snapEntry struct {
	size  int64
	mtime int64
}

func (im *Importer) snapshot() (map[string]snapEntry, error) {
	entries, _, err := walk.Walk(im.ImportDir, walk.Options{ExcludeTop: ExcludeTop, AllFiles: true})
	if err != nil {
		return nil, err
	}
	m := make(map[string]snapEntry, len(entries))
	for _, e := range entries {
		m[e.Rel] = snapEntry{e.Size, e.ModTime.UnixNano()}
	}
	return m, nil
}

// writeActivity compares two snapshots taken WriteWait apart.
func (im *Importer) writeActivity(ctx context.Context) (bool, error) {
	a, err := im.snapshot()
	if err != nil {
		return false, err
	}
	if err := im.Sleep(ctx, im.WriteWait); err != nil {
		return false, err
	}
	b, err := im.snapshot()
	if err != nil {
		return false, err
	}
	if len(a) != len(b) {
		return true, nil
	}
	for k, v := range a {
		if w, ok := b[k]; !ok || w != v {
			return true, nil
		}
	}
	return false, nil
}

// scan holds per-scan state.
type scan struct {
	im      *Importer
	start   time.Time
	spec    *perm.Spec
	batch   map[string]string // md5 -> absolute album path imported in this scan
	touched map[string]bool   // import sub-directories files were moved out of
	entries []importlog.Entry
	abort   string

	imported, dups, failed int
}

type verdict struct {
	dupType string // "" when not a duplicate
	ref     string // absolute album path of the conflicting file
	rename  bool   // same name, different content
}

func (sc *scan) logf(typ, src, ref string) {
	sc.entries = append(sc.entries, importlog.Entry{Type: typ, Source: src, Ref: ref})
}

func (sc *scan) fail(u unit, culprit *file, err error) {
	for _, f := range u.files {
		reason := err.Error()
		if f != culprit {
			reason = "Live Photo 另一半处理失败"
		}
		sc.logf(importlog.TypeFailed, f.Abs, reason)
		sc.failed++
	}
}

// unitDate returns the capture date that decides the target directory: the
// image's for a Live Photo, otherwise the file's; falls back to mtime.
func (sc *scan) unitDate(u unit) time.Time {
	f := u.files[0]
	if !f.Info.Taken.IsZero() {
		return f.Info.Taken.In(sc.im.Loc)
	}
	return f.ModTime.In(sc.im.Loc)
}

// DateDir returns the album sub-directory for a time: YYYY/YYYY-MM/YYYY-MM-DD.
func DateDir(t time.Time) string {
	return t.Format("2006") + "/" + t.Format("2006-01") + "/" + t.Format("2006-01-02")
}

func (sc *scan) albumAbs(rel string) string {
	return filepath.Join(sc.im.AlbumDir, filepath.FromSlash(rel))
}

// listNames returns lower-case name -> actual name for a directory.
func listNames(dir string) map[string]string {
	m := map[string]string{}
	ents, err := os.ReadDir(dir)
	if err != nil {
		return m
	}
	for _, e := range ents {
		m[strings.ToLower(e.Name())] = e.Name()
	}
	return m
}

// existingMD5 returns the MD5 of an album file, using the index when its
// size and mtime still match.
func (sc *scan) existingMD5(rel string) (string, error) {
	abs := sc.albumAbs(rel)
	st, err := os.Stat(abs)
	if err != nil {
		return "", err
	}
	if r, ok, err := sc.im.Store.GetFile(rel); err == nil && ok && r.Size == st.Size() && r.MtimeNs == st.ModTime().UnixNano() {
		return r.MD5, nil
	}
	return sc.im.Hash(abs)
}

func (sc *scan) process(u unit) error {
	sums := make([]string, len(u.files))
	for i, f := range u.files {
		s, err := sc.im.Hash(f.Abs)
		if err != nil {
			sc.fail(u, f, err)
			return nil
		}
		sums[i] = s
	}

	dateRel := DateDir(sc.unitDate(u))
	targetDir := sc.albumAbs(dateRel)
	names := listNames(targetDir)

	verdicts := make([]verdict, len(u.files))
	anyDup, allDup, anyRename := false, true, false
	for i, f := range u.files {
		v, err := sc.classify(f, sums[i], dateRel, names)
		if err != nil {
			return err
		}
		verdicts[i] = v
		if v.dupType != "" {
			anyDup = true
		} else {
			allDup = false
		}
		anyRename = anyRename || v.rename
	}

	if anyDup {
		return sc.toDuplicates(u, verdicts, allDup)
	}
	return sc.importUnit(u, sums, verdicts, dateRel, targetDir, names, anyRename)
}

func (sc *scan) classify(f *file, sum, dateRel string, names map[string]string) (verdict, error) {
	if p, ok := sc.batch[sum]; ok {
		return verdict{dupType: importlog.TypeBatchDup, ref: p}, nil
	}
	rename := false
	if actual, ok := names[strings.ToLower(f.name())]; ok {
		rel := dateRel + "/" + actual
		existing, err := sc.existingMD5(rel)
		if err == nil && existing == sum {
			return verdict{dupType: importlog.TypeSameNameDup, ref: sc.albumAbs(rel)}, nil
		}
		rename = true
	}
	rel, ok, err := sc.im.Store.FindByMD5(sum)
	if err != nil {
		return verdict{}, err
	}
	if ok {
		return verdict{dupType: importlog.TypeContentDup, ref: sc.albumAbs(rel)}, nil
	}
	return verdict{rename: rename}, nil
}

func (sc *scan) toDuplicates(u unit, vs []verdict, allDup bool) error {
	stamp := sc.start.In(sc.im.Loc).Format("20060102_150405")
	base := filepath.Join(sc.im.ImportDir, DuplicatesDir, stamp)
	type moved struct{ from, to string }
	var done []moved
	for _, f := range u.files {
		dst := filepath.Join(base, filepath.FromSlash(f.Rel))
		err := mover.MkdirAll(filepath.Dir(dst), sc.im.ImportDir, nil)
		if err == nil {
			err = mover.Move(f.Abs, dst, nil)
		}
		if err != nil {
			for i := len(done) - 1; i >= 0; i-- {
				_ = mover.Move(done[i].to, done[i].from, nil)
			}
			if mover.IsDiskFull(err) {
				sc.fail(u, f, err)
				return errAbort{abortDiskFull}
			}
			sc.fail(u, f, err)
			return nil
		}
		done = append(done, moved{f.Abs, dst})
	}
	for i, f := range u.files {
		sc.touched[filepath.Dir(f.Abs)] = true
		sc.dups++
		typ, ref := vs[i].dupType, vs[i].ref
		switch {
		case !u.live:
		case allDup:
			typ = importlog.LivePrefix + typ
		default:
			typ = importlog.TypeLivePartial
		}
		sc.logf(typ, f.Abs, ref)
	}
	return nil
}

func splitExt(name string) (string, string) {
	ext := path.Ext(name)
	return name[:len(name)-len(ext)], ext
}

func (sc *scan) importUnit(u unit, sums []string, vs []verdict, dateRel, targetDir string, names map[string]string, rename bool) error {
	final := make([]string, len(u.files))
	for i, f := range u.files {
		final[i] = f.name()
	}
	if rename {
		for n := 1; ; n++ {
			free := true
			for i, f := range u.files {
				stem, ext := splitExt(f.name())
				final[i] = fmt.Sprintf("%s_%d%s", stem, n, ext)
				if _, taken := names[strings.ToLower(final[i])]; taken {
					free = false
				}
			}
			if free {
				break
			}
		}
	}

	if err := mover.MkdirAll(targetDir, sc.im.AlbumDir, sc.spec); err != nil {
		sc.fail(u, u.files[0], err)
		if mover.IsDiskFull(err) {
			return errAbort{abortDiskFull}
		}
		return nil
	}

	type moved struct{ from, to string }
	var done []moved
	for i, f := range u.files {
		dst := filepath.Join(targetDir, final[i])
		if err := mover.Move(f.Abs, dst, sc.spec); err != nil {
			for j := len(done) - 1; j >= 0; j-- {
				_ = mover.Move(done[j].to, done[j].from, nil)
			}
			sc.fail(u, f, err)
			if mover.IsDiskFull(err) {
				return errAbort{abortDiskFull}
			}
			return nil
		}
		done = append(done, moved{f.Abs, dst})
	}

	for i, f := range u.files {
		dst := filepath.Join(targetDir, final[i])
		rel := dateRel + "/" + final[i]
		st, err := os.Stat(dst)
		if err != nil {
			return err
		}
		if err := sc.im.Store.UpsertFile(store.FileRec{RelPath: rel, Size: st.Size(), MtimeNs: st.ModTime().UnixNano(), MD5: sums[i]}); err != nil {
			return err
		}
		sc.batch[sums[i]] = dst
		names[strings.ToLower(final[i])] = final[i]
		sc.touched[filepath.Dir(f.Abs)] = true
		sc.imported++
		if final[i] != f.name() {
			typ := importlog.TypeRenamed
			if u.live {
				typ = importlog.LivePrefix + typ
			}
			sc.logf(typ, f.Abs, dst)
		}
	}
	_ = vs
	return nil
}
