// Command genicons writes the QPKG icons (64px, 80px and a gray 64px
// variant) into the given directory.
package main

import (
	"image"
	"image/color"
	"image/gif"
	"log"
	"os"
	"path/filepath"
)

func draw(size int, gray bool) *image.Paletted {
	bg := color.RGBA{0x25, 0x63, 0xeb, 0xff}
	sun := color.RGBA{0xfb, 0xbf, 0x24, 0xff}
	hill := color.RGBA{0xff, 0xff, 0xff, 0xff}
	if gray {
		bg, sun, hill = color.RGBA{0x9c, 0xa3, 0xaf, 0xff}, color.RGBA{0xd1, 0xd5, 0xdb, 0xff}, color.RGBA{0xf3, 0xf4, 0xf6, 0xff}
	}
	pal := color.Palette{color.Transparent, bg, sun, hill}
	img := image.NewPaletted(image.Rect(0, 0, size, size), pal)
	f := float64(size)
	r := f * 0.18 // corner radius
	inRounded := func(x, y float64) bool {
		cx, cy := x, y
		if x < r {
			cx = r
		} else if x > f-r {
			cx = f - r
		}
		if y < r {
			cy = r
		} else if y > f-r {
			cy = f - r
		}
		dx, dy := x-cx, y-cy
		return dx*dx+dy*dy <= r*r
	}
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			px, py := float64(x)+0.5, float64(y)+0.5
			if !inRounded(px, py) {
				continue
			}
			idx := uint8(1)
			// Sun.
			sx, sy, sr := f*0.70, f*0.30, f*0.10
			if (px-sx)*(px-sx)+(py-sy)*(py-sy) <= sr*sr {
				idx = 2
			}
			// Two hills (triangles).
			if py > f*0.80-(f*0.45-abs(px-f*0.38))*1.1 && py < f*0.82 {
				idx = 3
			}
			if py > f*0.80-(f*0.28-abs(px-f*0.68))*1.1 && py < f*0.82 {
				idx = 3
			}
			img.SetColorIndex(x, y, idx)
		}
	}
	return img
}

func abs(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}

func main() {
	if len(os.Args) != 2 {
		log.Fatal("usage: genicons <out-dir>")
	}
	dir := os.Args[1]
	if err := os.MkdirAll(dir, 0o755); err != nil {
		log.Fatal(err)
	}
	for name, spec := range map[string]struct {
		size int
		gray bool
	}{
		"photo-album.gif":      {64, false},
		"photo-album_80.gif":   {80, false},
		"photo-album_gray.gif": {64, true},
	} {
		f, err := os.Create(filepath.Join(dir, name))
		if err != nil {
			log.Fatal(err)
		}
		if err := gif.Encode(f, draw(spec.size, spec.gray), nil); err != nil {
			log.Fatal(err)
		}
		f.Close()
	}
}
