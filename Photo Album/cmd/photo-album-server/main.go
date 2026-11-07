package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"photo-album/internal/app"
	"photo-album/internal/fsbrowse"
	"photo-album/internal/metadata"
	"photo-album/internal/server"
	"photo-album/web"
)

func main() {
	exe, _ := os.Executable()
	defDataDir := filepath.Dir(filepath.Dir(exe)) // <QPKG_ROOT>/bin/photo-album-server -> <QPKG_ROOT>
	dataDir := flag.String("data-dir", defDataDir, "directory holding bootstrap.json (QPKG install dir)")
	listen := flag.String("listen", ":8090", "HTTP listen address")
	shareRoot := flag.String("share-root", "/share", "root of selectable shared folders")
	cleanup := flag.Bool("uninstall-cleanup", false, "remove <album>/.photo-album (called by the QPKG uninstaller) and exit")
	flag.Parse()

	if *cleanup {
		if err := uninstallCleanup(*dataDir); err != nil {
			log.Fatalf("uninstall-cleanup: %v", err)
		}
		return
	}

	lowerPriority()
	loc := metadata.NASLocation()
	log.Printf("photo-album: data-dir=%s listen=%s share-root=%s tz=%s", *dataDir, *listen, *shareRoot, loc)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	a := app.New(*dataDir, *shareRoot, loc)
	srv := &http.Server{
		Addr:              *listen,
		Handler:           server.New(a, &fsbrowse.Browser{Root: *shareRoot}, web.Dist()),
		ReadHeaderTimeout: 10 * time.Second,
	}
	go func() {
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatalf("http: %v", err)
		}
	}()

	done := make(chan struct{})
	go func() {
		defer close(done)
		if err := a.Run(ctx); err != nil {
			log.Printf("app: %v", err)
		}
	}()

	<-ctx.Done()
	log.Printf("photo-album: shutting down")
	shCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	_ = srv.Shutdown(shCtx)
	select {
	case <-done:
	case <-time.After(60 * time.Second):
		log.Printf("photo-album: scan did not stop in time")
	}
}
