#![allow(dead_code, unused_imports)]

pub mod folder;
pub mod match_result;
pub mod rule;
pub mod trash_config;

pub use folder::FolderItem;
pub use match_result::{AskItem, MatchListItem};
pub use rule::{DateAddedCondition, FolderRule};
pub use trash_config::TrashRuleConfig;
