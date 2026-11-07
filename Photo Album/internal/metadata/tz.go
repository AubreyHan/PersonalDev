package metadata

import (
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"
	_ "time/tzdata" // embedded zone database for static builds
)

// NASLocation returns the NAS system time zone.
//
// Order:
//  1. PHOTO_ALBUM_TZ env (IANA name, e.g. set by the QPKG start script)
//  2. TZ env as an IANA name
//  3. /etc/localtime
//  4. TZ env as a simple POSIX string such as "CST-8" (QTS style)
//  5. time.Local
func NASLocation() *time.Location {
	return nasLocation(os.Getenv, "/etc/localtime")
}

func nasLocation(getenv func(string) string, localtime string) *time.Location {
	for _, env := range []string{"PHOTO_ALBUM_TZ", "TZ"} {
		name := strings.TrimPrefix(strings.TrimSpace(getenv(env)), ":")
		if name == "" {
			continue
		}
		if loc, err := time.LoadLocation(name); err == nil {
			return loc
		}
	}
	if b, err := os.ReadFile(localtime); err == nil {
		if loc, err := time.LoadLocationFromTZData("Local", b); err == nil {
			return loc
		}
	}
	if loc, ok := parsePOSIXTZ(getenv("TZ")); ok {
		return loc
	}
	return time.Local
}

var posixTZ = regexp.MustCompile(`^([A-Za-z]{3,}|<[^>]+>)([+-]?)(\d{1,2})(?::(\d{2}))?`)

// parsePOSIXTZ handles the standard-time part of a POSIX TZ string. Note the
// inverted sign: "CST-8" is UTC+8. DST rules are ignored.
func parsePOSIXTZ(s string) (*time.Location, bool) {
	m := posixTZ.FindStringSubmatch(strings.TrimSpace(s))
	if m == nil {
		return nil, false
	}
	h, _ := strconv.Atoi(m[3])
	mins := 0
	if m[4] != "" {
		mins, _ = strconv.Atoi(m[4])
	}
	off := h*3600 + mins*60
	if m[2] != "-" {
		off = -off
	}
	return time.FixedZone(strings.Trim(m[1], "<>"), off), true
}
