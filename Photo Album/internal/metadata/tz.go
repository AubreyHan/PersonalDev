package metadata

import (
	"os"
	"strings"
	"time"
	_ "time/tzdata" // embedded zone database for static builds
)

// NASLocation returns the NAS system time zone.
//
// Order: PHOTO_ALBUM_TZ env (IANA name, set by the QPKG start script),
// TZ env, then the Go default (/etc/localtime). Falls back to time.Local.
func NASLocation() *time.Location {
	for _, env := range []string{"PHOTO_ALBUM_TZ", "TZ"} {
		name := strings.TrimPrefix(strings.TrimSpace(os.Getenv(env)), ":")
		if name == "" {
			continue
		}
		if loc, err := time.LoadLocation(name); err == nil {
			return loc
		}
	}
	return time.Local
}
