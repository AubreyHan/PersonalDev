#![allow(unused_imports, dead_code)]

pub mod date_added;
pub mod path_hierarchy_guard;
pub mod pattern_matcher;
pub mod trash_controller;
pub mod waterfall_engine;

pub use date_added::get_date_added;
pub use path_hierarchy_guard::{GuardError, PathHierarchyGuard};
pub use pattern_matcher::PatternMatcher;
pub use trash_controller::{TrashCleanupReport, TrashController, TrashStats};
pub use waterfall_engine::{WaterfallEngine, WaterfallEngineResult};
