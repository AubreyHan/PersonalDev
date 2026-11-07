// Command genicons renders the QPKG icons from the master artwork.
//
//	go run ./scripts/genicons <master.png> <qpkg-icons-dir> [favicon.png]
//
// Outputs photo-album.gif (64px), photo-album_80.gif (80px) and
// photo-album_gray.gif (64px, grayscale, shown when the app is disabled),
// each with rounded transparent corners; optionally a 128px PNG favicon.
package main

import (
	"image"
	"image/color"
	"image/gif"
	_ "image/jpeg"
	"image/png"
	"log"
	"math"
	"os"
	"path/filepath"
	"sort"

	"golang.org/x/image/draw"
)

func load(p string) image.Image {
	f, err := os.Open(p)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	img, _, err := image.Decode(f)
	if err != nil {
		log.Fatal(err)
	}
	return img
}

// render scales the master to size and applies a rounded-square alpha mask
// (corner radius 22% like QTS app icons), optionally converting to gray.
func render(src image.Image, size int, gray bool) *image.NRGBA {
	b := src.Bounds()
	side := b.Dx()
	if b.Dy() < side {
		side = b.Dy()
	}
	crop := image.Rect(b.Min.X+(b.Dx()-side)/2, b.Min.Y+(b.Dy()-side)/2, 0, 0)
	crop.Max = crop.Min.Add(image.Pt(side, side))
	dst := image.NewNRGBA(image.Rect(0, 0, size, size))
	draw.CatmullRom.Scale(dst, dst.Bounds(), src, crop, draw.Src, nil)

	f := float64(size)
	r := f * 0.22
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			// Signed distance to the rounded square, 4x4 supersampled coverage.
			cov := 0.0
			for sy := 0; sy < 4; sy++ {
				for sx := 0; sx < 4; sx++ {
					px := float64(x) + (float64(sx)+0.5)/4
					py := float64(y) + (float64(sy)+0.5)/4
					cx := math.Min(math.Max(px, r), f-r)
					cy := math.Min(math.Max(py, r), f-r)
					if (px-cx)*(px-cx)+(py-cy)*(py-cy) <= r*r {
						cov++
					}
				}
			}
			c := dst.NRGBAAt(x, y)
			c.A = uint8(float64(c.A) * cov / 16)
			if gray {
				l := uint8(0.299*float64(c.R) + 0.587*float64(c.G) + 0.114*float64(c.B))
				// Lighten so the disabled state reads as inactive.
				l = uint8(128 + int(l)/2)
				c.R, c.G, c.B = l, l, l
			}
			dst.SetNRGBA(x, y, c)
		}
	}
	return dst
}

// medianCut builds an adaptive palette of n colors from opaque pixels.
func medianCut(img *image.NRGBA, n int) color.Palette {
	var px [][3]uint8
	for i := 0; i < len(img.Pix); i += 4 {
		if img.Pix[i+3] >= 128 {
			px = append(px, [3]uint8{img.Pix[i], img.Pix[i+1], img.Pix[i+2]})
		}
	}
	boxes := [][][3]uint8{px}
	for len(boxes) < n {
		// Split the box with the largest channel range.
		bi, bc, br := -1, 0, -1
		for i, bx := range boxes {
			if len(bx) < 2 {
				continue
			}
			for c := 0; c < 3; c++ {
				lo, hi := uint8(255), uint8(0)
				for _, p := range bx {
					if p[c] < lo {
						lo = p[c]
					}
					if p[c] > hi {
						hi = p[c]
					}
				}
				if int(hi-lo) > br {
					bi, bc, br = i, c, int(hi-lo)
				}
			}
		}
		if bi < 0 || br == 0 {
			break
		}
		bx := boxes[bi]
		sort.Slice(bx, func(i, j int) bool { return bx[i][bc] < bx[j][bc] })
		mid := len(bx) / 2
		boxes[bi] = bx[:mid]
		boxes = append(boxes, bx[mid:])
	}
	pal := color.Palette{color.NRGBA{0, 0, 0, 0}}
	for _, bx := range boxes {
		if len(bx) == 0 {
			continue
		}
		var s [3]int
		for _, p := range bx {
			s[0] += int(p[0])
			s[1] += int(p[1])
			s[2] += int(p[2])
		}
		l := len(bx)
		pal = append(pal, color.NRGBA{uint8(s[0] / l), uint8(s[1] / l), uint8(s[2] / l), 255})
	}
	return pal
}

func writeGIF(path string, img *image.NRGBA) {
	pal := medianCut(img, 255)
	out := image.NewPaletted(img.Bounds(), pal)
	opaque := pal[1:]
	for y := 0; y < img.Bounds().Dy(); y++ {
		for x := 0; x < img.Bounds().Dx(); x++ {
			c := img.NRGBAAt(x, y)
			if c.A < 128 {
				out.SetColorIndex(x, y, 0)
				continue
			}
			out.SetColorIndex(x, y, uint8(1+opaque.Index(color.NRGBA{c.R, c.G, c.B, 255})))
		}
	}
	f, err := os.Create(path)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	if err := gif.Encode(f, out, nil); err != nil {
		log.Fatal(err)
	}
}

func writePNG(path string, img image.Image) {
	f, err := os.Create(path)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	if err := png.Encode(f, img); err != nil {
		log.Fatal(err)
	}
}

func main() {
	if len(os.Args) < 3 {
		log.Fatal("usage: genicons <master.png> <out-dir> [favicon.png]")
	}
	src := load(os.Args[1])
	dir := os.Args[2]
	if err := os.MkdirAll(dir, 0o755); err != nil {
		log.Fatal(err)
	}
	writeGIF(filepath.Join(dir, "photo-album.gif"), render(src, 64, false))
	writeGIF(filepath.Join(dir, "photo-album_80.gif"), render(src, 80, false))
	writeGIF(filepath.Join(dir, "photo-album_gray.gif"), render(src, 64, true))
	if len(os.Args) > 3 {
		writePNG(os.Args[3], render(src, 128, false))
	}
}
