// Package hashing computes file MD5 digests with sequential 1 MiB reads.
package hashing

import (
	"crypto/md5"
	"encoding/hex"
	"io"
	"os"
)

const bufSize = 1 << 20

// MD5File returns the lowercase hex MD5 of the file at path.
func MD5File(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := md5.New()
	buf := make([]byte, bufSize)
	if _, err := io.CopyBuffer(h, f, buf); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}
