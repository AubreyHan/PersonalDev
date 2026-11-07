// Package app wires the components together and runs the startup state
// machine (ARCHITECTURE §3).
package app

import (
	"context"
	"errors"
	"fmt"
	"log"
	"sync"
	"time"

	"photo-album/internal/bootstrap"
	"photo-album/internal/importer"
	"photo-album/internal/importlog"
	"photo-album/internal/indexer"
	"photo-album/internal/scheduler"
	"photo-album/internal/setup"
	"photo-album/internal/store"
)

// Phase is the externally visible state.
type Phase string

const (
	PhaseUnconfigured       Phase = "unconfigured"
	PhaseStorageUnavailable Phase = "storage_unavailable"
	PhaseIndexing           Phase = "indexing"
	PhaseRunning            Phase = "running"
)

// ErrAlreadyConfigured is returned by Setup after the first run.
var ErrAlreadyConfigured = errors.New("已完成首次配置")

// App is the running application.
type App struct {
	DataDir    string // QPKG install dir holding bootstrap.json
	ShareRoot  string
	Loc        *time.Location
	RetryEvery time.Duration
	// Tune, when set, may adjust components before scanning starts (tests).
	Tune func(*importer.Importer, *scheduler.Scheduler)

	mu      sync.RWMutex
	setupMu sync.Mutex
	phase   Phase
	lastErr string
	st      *store.Store
	sched   *scheduler.Scheduler
	setupCh chan struct{}
}

// New returns an App.
func New(dataDir, shareRoot string, loc *time.Location) *App {
	return &App{
		DataDir: dataDir, ShareRoot: shareRoot, Loc: loc,
		RetryEvery: time.Minute,
		phase:      PhaseUnconfigured,
		setupCh:    make(chan struct{}, 1),
	}
}

func (a *App) setPhase(p Phase, err error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.phase = p
	if err != nil {
		a.lastErr = err.Error()
		log.Printf("app: %s: %v", p, err)
	} else {
		a.lastErr = ""
	}
}

func (a *App) setErr(err error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	if err == nil {
		a.lastErr = ""
	} else {
		a.lastErr = err.Error()
	}
}

func wait(ctx context.Context, d time.Duration, wake <-chan struct{}) error {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-t.C:
	case <-wake:
	}
	return nil
}

// Run blocks until ctx is cancelled.
func (a *App) Run(ctx context.Context) error {
	for {
		cfg, err := bootstrap.Load(a.DataDir)
		if errors.Is(err, bootstrap.ErrNotConfigured) {
			a.setPhase(PhaseUnconfigured, nil)
			if err := wait(ctx, 24*time.Hour, a.setupCh); err != nil {
				return nil
			}
			continue
		}
		if err == nil {
			var st *store.Store
			st, err = store.Open(cfg.AlbumDir, false)
			if err == nil {
				a.runConfigured(ctx, st, cfg.AlbumDir)
				st.Close()
				return nil
			}
		}
		a.setPhase(PhaseStorageUnavailable, err)
		if err := wait(ctx, a.RetryEvery, nil); err != nil {
			return nil
		}
	}
}

func (a *App) runConfigured(ctx context.Context, st *store.Store, albumDir string) {
	a.mu.Lock()
	a.st = st
	a.mu.Unlock()

	importDir, _ := st.Get(store.KeyImportDir)
	ix := indexer.New(st, albumDir)

	for {
		done, err := st.GetBool(store.KeyInitialIndexDone)
		if err == nil && done {
			break
		}
		a.setPhase(PhaseIndexing, nil)
		if err == nil {
			if err = ix.Initial(ctx); err == nil {
				// Scan immediately after the initial index (PRD N).
				err = st.Set(store.KeyNextScanAt, "")
				if err == nil {
					break
				}
			}
		}
		if ctx.Err() != nil {
			return
		}
		a.setErr(err)
		log.Printf("app: initial index: %v", err)
		if wait(ctx, a.RetryEvery, nil) != nil {
			return
		}
	}

	im := importer.New(st, ix, albumDir, importDir, a.Loc)
	sched := scheduler.New(st,
		func(ctx context.Context, start time.Time) {
			res, err := im.Run(ctx, start)
			if err != nil {
				log.Printf("scan: %v", err)
			} else {
				log.Printf("scan: %+v", res)
			}
			a.setErr(err)
		},
		func(t time.Time) {
			if err := im.Log.WriteSkip(t, importlog.ReasonBusy); err != nil {
				log.Printf("scan: write skip: %v", err)
			}
		})
	a.mu.Lock()
	a.sched = sched
	a.mu.Unlock()
	if a.Tune != nil {
		a.Tune(im, sched)
	}
	a.setPhase(PhaseRunning, nil)
	sched.Run(ctx)
}

// Setup performs the first-run configuration.
func (a *App) Setup(req setup.Request) error {
	a.setupMu.Lock()
	defer a.setupMu.Unlock()
	a.mu.RLock()
	phase := a.phase
	a.mu.RUnlock()
	if phase != PhaseUnconfigured {
		return ErrAlreadyConfigured
	}
	if _, err := bootstrap.Load(a.DataDir); !errors.Is(err, bootstrap.ErrNotConfigured) {
		return ErrAlreadyConfigured
	}
	album, imp, err := setup.Validate(a.ShareRoot, req)
	if err != nil {
		return err
	}
	st, err := store.Open(album, true)
	if err != nil {
		return err
	}
	for k, v := range map[string]string{
		store.KeyAlbumDir:  album,
		store.KeyImportDir: imp,
	} {
		if err := st.Set(k, v); err != nil {
			st.Close()
			return err
		}
	}
	if err := st.SetInt(store.KeyIntervalMinutes, req.IntervalMinutes); err != nil {
		st.Close()
		return err
	}
	if err := st.SetBool(store.KeyInitialIndexDone, false); err != nil {
		st.Close()
		return err
	}
	if err := st.Close(); err != nil {
		return err
	}
	if err := bootstrap.Save(a.DataDir, bootstrap.Config{AlbumDir: album}); err != nil {
		return err
	}
	select {
	case a.setupCh <- struct{}{}:
	default:
	}
	return nil
}

// SetInterval changes the scan interval. While the initial index is still
// running it is only stored; the scheduler picks it up when it starts.
func (a *App) SetInterval(minutes int64) error {
	a.mu.RLock()
	st, sched := a.st, a.sched
	a.mu.RUnlock()
	if sched != nil {
		return sched.SetInterval(minutes)
	}
	if st == nil {
		return errors.New("尚未完成配置")
	}
	if !scheduler.ValidInterval(minutes) {
		return fmt.Errorf("扫描间隔必须在 %d–%d 分钟之间", scheduler.MinInterval, scheduler.MaxInterval)
	}
	return st.SetInt(store.KeyIntervalMinutes, minutes)
}

// Status is the status-page payload.
type Status struct {
	Phase              Phase      `json:"phase"`
	Error              string     `json:"error,omitempty"`
	AlbumDir           string     `json:"album_dir,omitempty"`
	ImportDir          string     `json:"import_dir,omitempty"`
	IntervalMinutes    int64      `json:"interval_minutes,omitempty"`
	InitialIndexDone   bool       `json:"initial_index_done"`
	IndexDone          int64      `json:"index_done"`
	IndexTotal         int64      `json:"index_total"`
	LastScanStartedAt  *time.Time `json:"last_scan_started_at,omitempty"`
	LastScanFinishedAt *time.Time `json:"last_scan_finished_at,omitempty"`
	NextScanAt         *time.Time `json:"next_scan_at,omitempty"`
	Scanning           bool       `json:"scanning"`
}

// Phase returns the current phase.
func (a *App) Phase() Phase {
	a.mu.RLock()
	defer a.mu.RUnlock()
	return a.phase
}

// Status returns the current status.
func (a *App) Status() Status {
	a.mu.RLock()
	s := Status{Phase: a.phase, Error: a.lastErr}
	st, sched := a.st, a.sched
	a.mu.RUnlock()
	if st == nil {
		return s
	}
	s.AlbumDir, _ = st.Get(store.KeyAlbumDir)
	s.ImportDir, _ = st.Get(store.KeyImportDir)
	s.IntervalMinutes, _ = st.GetInt(store.KeyIntervalMinutes)
	s.InitialIndexDone, _ = st.GetBool(store.KeyInitialIndexDone)
	s.IndexDone, _ = st.GetInt(store.KeyIndexDone)
	s.IndexTotal, _ = st.GetInt(store.KeyIndexTotal)
	tp := func(k string) *time.Time {
		t, err := st.GetTime(k)
		if err != nil || t.IsZero() {
			return nil
		}
		t = t.In(a.Loc)
		return &t
	}
	s.LastScanStartedAt = tp(store.KeyLastScanStartedAt)
	s.LastScanFinishedAt = tp(store.KeyLastScanFinishedAt)
	if s.Phase == PhaseRunning {
		s.NextScanAt = tp(store.KeyNextScanAt)
	}
	if sched != nil {
		s.Scanning = sched.Scanning()
	}
	return s
}
