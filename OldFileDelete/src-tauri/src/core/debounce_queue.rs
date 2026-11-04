use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use tauri::async_runtime::JoinHandle;
use tokio::sync::{mpsc, Mutex};

pub struct DebounceQueue {
    debounce_duration: Duration,
    active_tasks: Arc<Mutex<HashMap<PathBuf, JoinHandle<()>>>>,
    notify_tx: mpsc::Sender<PathBuf>,
}

impl DebounceQueue {
    pub fn new(debounce_duration: Duration, notify_tx: mpsc::Sender<PathBuf>) -> Self {
        Self {
            debounce_duration,
            active_tasks: Arc::new(Mutex::new(HashMap::new())),
            notify_tx,
        }
    }

    /// Trigger a debounced notification for a folder path.
    /// Resets the countdown if triggered again within the debounce window.
    pub async fn trigger(&self, folder_path: PathBuf) {
        let mut tasks = self.active_tasks.lock().await;

        // Cancel existing pending countdown for this folder
        if let Some(handle) = tasks.remove(&folder_path) {
            handle.abort();
        }

        let duration = self.debounce_duration;
        let tx = self.notify_tx.clone();
        let path_clone = folder_path.clone();
        let tasks_map = Arc::clone(&self.active_tasks);

        let handle = tokio::spawn(async move {
            tokio::time::sleep(duration).await;
            // Clean up from active map
            let mut map = tasks_map.lock().await;
            map.remove(&path_clone);
            drop(map);

            // Send notification
            let _ = tx.send(path_clone).await;
        });

        tasks.insert(folder_path, handle);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_debounce_coalesces_rapid_events() {
        let (tx, mut rx) = mpsc::channel(10);
        // Use 100ms debounce for quick test
        let queue = DebounceQueue::new(Duration::from_millis(100), tx);
        let path = PathBuf::from("/test/folder");

        // Fire 5 rapid triggers in 20ms intervals
        for _ in 0..5 {
            queue.trigger(path.clone()).await;
            tokio::time::sleep(Duration::from_millis(20)).await;
        }

        // Wait past the debounce duration
        let result = tokio::time::timeout(Duration::from_millis(300), rx.recv()).await;
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), Some(path));

        // Ensure NO second event is received
        let second_result = tokio::time::timeout(Duration::from_millis(50), rx.recv()).await;
        assert!(second_result.is_err()); // Timeout means only 1 event arrived!
    }
}
