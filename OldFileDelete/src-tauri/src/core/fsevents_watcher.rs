use notify::{Event, EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;

use super::debounce_queue::DebounceQueue;

pub struct FsEventsWatcher {
    watcher: RecommendedWatcher,
    watched_paths: HashSet<PathBuf>,
    debounce_queue: Arc<DebounceQueue>,
}

impl FsEventsWatcher {
    /// Create a new FsEventsWatcher with a given debounce duration (default 3 seconds)
    pub fn new(
        debounce_duration: Duration,
        scan_trigger_tx: mpsc::Sender<PathBuf>,
    ) -> Result<Self, notify::Error> {
        let debounce_queue = Arc::new(DebounceQueue::new(debounce_duration, scan_trigger_tx));
        let queue_clone = Arc::clone(&debounce_queue);

        // Tokio runtime handle for bridging synchronous notify callback to async debounce queue
        let rt = tokio::runtime::Handle::current();

        let watcher = RecommendedWatcher::new(
            move |res: Result<Event, notify::Error>| {
                if let Ok(event) = res {
                    match event.kind {
                        EventKind::Create(_) | EventKind::Modify(_) | EventKind::Remove(_) => {
                            for path in event.paths {
                                // Ignore changes purely to hidden files
                                if let Some(name) = path.file_name().and_then(|n| n.to_str()) {
                                    if name.starts_with('.') {
                                        continue;
                                    }
                                }

                                // Find the folder to notify
                                let target_folder = if path.is_dir() {
                                    path
                                } else {
                                    path.parent().map(|p| p.to_path_buf()).unwrap_or(path)
                                };

                                let q = Arc::clone(&queue_clone);
                                rt.spawn(async move {
                                    q.trigger(target_folder).await;
                                });
                            }
                        }
                        _ => {}
                    }
                }
            },
            notify::Config::default(),
        )?;

        Ok(Self {
            watcher,
            watched_paths: HashSet::new(),
            debounce_queue,
        })
    }

    /// Add a directory to watch recursively
    pub fn watch_folder(&mut self, path: &Path) -> Result<(), notify::Error> {
        let canonical = path.canonicalize().unwrap_or_else(|_| path.to_path_buf());
        if !self.watched_paths.contains(&canonical) {
            self.watcher.watch(&canonical, RecursiveMode::Recursive)?;
            self.watched_paths.insert(canonical);
        }
        Ok(())
    }

    /// Remove a directory from watch
    pub fn unwatch_folder(&mut self, path: &Path) -> Result<(), notify::Error> {
        let canonical = path.canonicalize().unwrap_or_else(|_| path.to_path_buf());
        if self.watched_paths.remove(&canonical) {
            let _ = self.watcher.unwatch(&canonical);
        }
        Ok(())
    }

    /// Sync the set of monitored folders
    pub fn sync_watched_folders(&mut self, desired_paths: &[PathBuf]) -> Result<(), notify::Error> {
        let desired_set: HashSet<PathBuf> = desired_paths
            .iter()
            .map(|p| p.canonicalize().unwrap_or_else(|_| p.clone()))
            .collect();

        // Remove paths not in desired
        let current_paths: Vec<PathBuf> = self.watched_paths.iter().cloned().collect();
        for p in current_paths {
            if !desired_set.contains(&p) {
                self.unwatch_folder(&p)?;
            }
        }

        // Add new paths
        for p in desired_set {
            if !self.watched_paths.contains(&p) && p.exists() {
                self.watch_folder(&p)?;
            }
        }

        Ok(())
    }
}

/// Spawns an hourly background heartbeat to trigger scheduled checks for natural time-based expiration.
pub fn spawn_hourly_heartbeat(trigger_tx: mpsc::Sender<()>) -> tokio::task::JoinHandle<()> {
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_secs(3600));
        // First tick completes immediately, skip it
        interval.tick().await;

        loop {
            interval.tick().await;
            let _ = trigger_tx.send(()).await;
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{self, File};
    use std::io::Write;

    #[tokio::test]
    async fn test_fsevents_watcher_triggers_on_file_change() {
        let temp_dir = std::env::temp_dir().join("file_purger_fsevents_test");
        let _ = fs::remove_dir_all(&temp_dir);
        fs::create_dir_all(&temp_dir).unwrap();

        let (tx, mut rx) = mpsc::channel(10);
        // Fast 100ms debounce for unit test
        let mut watcher = FsEventsWatcher::new(Duration::from_millis(100), tx).unwrap();
        watcher.watch_folder(&temp_dir).unwrap();

        // Create a file in the watched directory
        let test_file = temp_dir.join("test_file.txt");
        let mut f = File::create(&test_file).unwrap();
        f.write_all(b"hello fsevents").unwrap();
        drop(f);

        // Wait for debounce notification
        let event = tokio::time::timeout(Duration::from_secs(2), rx.recv()).await;
        assert!(event.is_ok(), "Expected watcher to receive debounced event");
        let path = event.unwrap().unwrap();
        assert!(
            path.starts_with(&temp_dir) || path == temp_dir,
            "Received path {:?} should be inside {:?}",
            path,
            temp_dir
        );

        let _ = fs::remove_dir_all(&temp_dir);
    }
}
