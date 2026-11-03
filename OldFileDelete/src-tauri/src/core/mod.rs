#![allow(unused_imports, dead_code)]

pub mod date_added;
pub mod debounce_queue;
pub mod fsevents_watcher;
pub mod path_hierarchy_guard;
pub mod pattern_matcher;
pub mod trash_controller;
pub mod waterfall_engine;

pub use date_added::get_date_added;
pub use debounce_queue::DebounceQueue;
pub use fsevents_watcher::{spawn_hourly_heartbeat, FsEventsWatcher};
pub use path_hierarchy_guard::{GuardError, PathHierarchyGuard};
pub use pattern_matcher::PatternMatcher;
pub use trash_controller::{TrashCleanupReport, TrashController, TrashStats};
pub use waterfall_engine::{WaterfallEngine, WaterfallEngineResult};
