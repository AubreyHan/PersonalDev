package metadata

import (
	"testing"
	"time"
)

func TestNASLocation(t *testing.T) {
	env := func(m map[string]string) func(string) string { return func(k string) string { return m[k] } }

	if loc := nasLocation(env(map[string]string{"PHOTO_ALBUM_TZ": "Asia/Shanghai"}), "/nonexistent"); loc.String() != "Asia/Shanghai" {
		t.Fatal(loc)
	}
	loc := nasLocation(env(map[string]string{"TZ": "CST-8"}), "/nonexistent")
	if _, off := time.Now().In(loc).Zone(); off != 8*3600 {
		t.Fatalf("CST-8 offset %d", off)
	}
	loc = nasLocation(env(map[string]string{"TZ": "EST5EDT"}), "/nonexistent")
	if loc.String() != "EST5EDT" { // valid IANA legacy name
		if _, off := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC).In(loc).Zone(); off != -5*3600 {
			t.Fatal(loc, off)
		}
	}
	loc = nasLocation(env(map[string]string{"TZ": "<+0530>-5:30"}), "/nonexistent")
	if _, off := time.Now().In(loc).Zone(); off != 5*3600+1800 {
		t.Fatal(off)
	}
}
