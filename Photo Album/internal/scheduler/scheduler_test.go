package scheduler

import (
	"context"
	"sync"
	"testing"
	"time"

	"photo-album/internal/store"
)

type fakeClock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *fakeClock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return c.t }
func (c *fakeClock) Add(d time.Duration) {
	c.mu.Lock()
	c.t = c.t.Add(d)
	c.mu.Unlock()
}

func setup(t *testing.T) (*Scheduler, *fakeClock, *[]time.Time, *[]time.Time, chan struct{}) {
	t.Helper()
	st, err := store.Open(t.TempDir(), true)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { st.Close() })
	st.SetInt(store.KeyIntervalMinutes, 60)
	clk := &fakeClock{t: time.Date(2026, 11, 7, 10, 0, 0, 0, time.UTC)}
	var mu sync.Mutex
	var scans, busy []time.Time
	release := make(chan struct{}, 10)
	s := New(st, func(ctx context.Context, start time.Time) {
		mu.Lock()
		scans = append(scans, start)
		mu.Unlock()
		<-release
	}, func(t time.Time) { busy = append(busy, t) })
	s.Now = clk.Now
	return s, clk, &scans, &busy, release
}

func TestScheduling(t *testing.T) {
	s, clk, scans, busy, release := setup(t)
	ctx := context.Background()

	// next_scan_at unset (just finished initial index) -> scan immediately.
	s.Check(ctx)
	release <- struct{}{}
	s.Wait()
	if len(*scans) != 1 {
		t.Fatalf("scans=%v", *scans)
	}
	next, _ := s.Store.GetTime(store.KeyNextScanAt)
	if !next.Equal(clk.Now().Add(time.Hour)) {
		t.Fatal(next)
	}

	// Not yet due.
	clk.Add(59 * time.Minute)
	s.Check(ctx)
	if len(*scans) != 1 {
		t.Fatal("scanned too early")
	}

	// Due: starts a long-running scan; next is measured from the start.
	clk.Add(time.Minute)
	s.Check(ctx)
	if len(*scans) != 2 || !s.Scanning() {
		t.Fatal("expected second scan running")
	}
	// An hour later the scan still runs -> skipped and logged.
	clk.Add(time.Hour)
	s.Check(ctx)
	if len(*busy) != 1 || len(*scans) != 2 {
		t.Fatalf("busy=%v scans=%v", *busy, *scans)
	}
	release <- struct{}{}
	s.Wait()

	// Interval change takes effect immediately from now.
	if err := s.SetInterval(4); err == nil {
		t.Fatal("4 minutes must be rejected")
	}
	if err := s.SetInterval(10); err != nil {
		t.Fatal(err)
	}
	next, _ = s.Store.GetTime(store.KeyNextScanAt)
	if !next.Equal(clk.Now().Add(10 * time.Minute)) {
		t.Fatal(next)
	}
	clk.Add(10 * time.Minute)
	s.Check(ctx)
	release <- struct{}{}
	s.Wait()
	if len(*scans) != 3 {
		t.Fatal(*scans)
	}
	if fin, _ := s.Store.GetTime(store.KeyLastScanFinishedAt); fin.IsZero() {
		t.Fatal("finish time not recorded")
	}
}

func TestRestartPastDueScansImmediately(t *testing.T) {
	s, clk, scans, _, release := setup(t)
	s.Store.SetTime(store.KeyNextScanAt, clk.Now().Add(-3*time.Hour)) // NAS was off
	s.Check(context.Background())
	release <- struct{}{}
	s.Wait()
	if len(*scans) != 1 {
		t.Fatal(*scans)
	}

	s2, clk2, scans2, _, _ := setup(t)
	s2.Store.SetTime(store.KeyNextScanAt, clk2.Now().Add(20*time.Minute))
	s2.Check(context.Background())
	if len(*scans2) != 0 {
		t.Fatal("should wait until next_scan_at")
	}
}
