// Package scheduler triggers scans at a fixed interval measured from the
// start of the previous scan, with at most one scan running at a time
// (PRD §3 扫描频率/扫描计时/扫描并发, ARCHITECTURE §5.11).
package scheduler

import (
	"context"
	"fmt"
	"log"
	"sync"
	"sync/atomic"
	"time"

	"photo-album/internal/store"
)

// Interval bounds in minutes.
const (
	MinInterval     = 5
	MaxInterval     = 1440
	DefaultInterval = 60
)

// Scheduler decides when to scan.
type Scheduler struct {
	Store *store.Store
	Now   func() time.Time
	// Scan runs one scan synchronously.
	Scan func(ctx context.Context, start time.Time)
	// OnBusy is called when a scan is due while the previous one still runs.
	OnBusy func(t time.Time)
	Tick   time.Duration

	mu      sync.Mutex // serialises Check / SetInterval bookkeeping
	running atomic.Bool
	wg      sync.WaitGroup
}

// New returns a Scheduler with a 15 s tick.
func New(s *store.Store, scan func(ctx context.Context, start time.Time), onBusy func(time.Time)) *Scheduler {
	return &Scheduler{Store: s, Now: time.Now, Scan: scan, OnBusy: onBusy, Tick: 15 * time.Second}
}

// ValidInterval reports whether m is an allowed interval in minutes.
func ValidInterval(m int64) bool { return m >= MinInterval && m <= MaxInterval }

func (s *Scheduler) interval() time.Duration {
	m, err := s.Store.GetInt(store.KeyIntervalMinutes)
	if err != nil || !ValidInterval(m) {
		m = DefaultInterval
	}
	return time.Duration(m) * time.Minute
}

// Run checks every Tick until ctx is done, then waits for a running scan.
func (s *Scheduler) Run(ctx context.Context) {
	t := time.NewTicker(s.Tick)
	defer t.Stop()
	s.Check(ctx)
	for {
		select {
		case <-ctx.Done():
			s.wg.Wait()
			return
		case <-t.C:
			s.Check(ctx)
		}
	}
}

// Check starts a scan when next_scan_at has passed (a missing value means
// "due now", e.g. right after the initial index or a restart past due).
func (s *Scheduler) Check(ctx context.Context) {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := s.Now()
	next, err := s.Store.GetTime(store.KeyNextScanAt)
	if err != nil {
		log.Printf("scheduler: %v", err)
		return
	}
	if !next.IsZero() && now.Before(next) {
		return
	}
	iv := s.interval()
	if !s.running.CompareAndSwap(false, true) {
		if s.OnBusy != nil {
			s.OnBusy(now)
		}
		s.setTime(store.KeyNextScanAt, now.Add(iv))
		return
	}
	s.setTime(store.KeyLastScanStartedAt, now)
	s.setTime(store.KeyNextScanAt, now.Add(iv))
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		defer s.running.Store(false)
		s.Scan(ctx, now)
		s.setTime(store.KeyLastScanFinishedAt, s.Now())
	}()
}

// SetInterval stores a new interval and reschedules the next scan from now.
// A running scan is not affected.
func (s *Scheduler) SetInterval(minutes int64) error {
	if !ValidInterval(minutes) {
		return fmt.Errorf("扫描间隔必须在 %d–%d 分钟之间", MinInterval, MaxInterval)
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if err := s.Store.SetInt(store.KeyIntervalMinutes, minutes); err != nil {
		return err
	}
	return s.Store.SetTime(store.KeyNextScanAt, s.Now().Add(time.Duration(minutes)*time.Minute))
}

// Scanning reports whether a scan is running.
func (s *Scheduler) Scanning() bool { return s.running.Load() }

// Wait blocks until a running scan finishes.
func (s *Scheduler) Wait() { s.wg.Wait() }

func (s *Scheduler) setTime(key string, t time.Time) {
	if err := s.Store.SetTime(key, t); err != nil {
		log.Printf("scheduler: set %s: %v", key, err)
	}
}
