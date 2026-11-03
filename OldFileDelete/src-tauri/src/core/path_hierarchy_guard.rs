use std::path::{Path, PathBuf};
use thiserror::Error;

#[derive(Error, Debug, PartialEq, Eq)]
pub enum GuardError {
    #[error("Path '{0}' does not exist")]
    PathNotFound(PathBuf),

    #[error("Path '{0}' is not a directory")]
    NotADirectory(PathBuf),

    #[error("Path '{0}' is already being monitored")]
    AlreadyAdded(PathBuf),

    #[error("Cannot add parent directory '{new_path}' because child directory '{existing}' is already being monitored")]
    ParentOfExisting {
        new_path: PathBuf,
        existing: PathBuf,
    },

    #[error("Cannot add subdirectory '{new_path}' because parent directory '{existing}' is already being monitored")]
    ChildOfExisting {
        new_path: PathBuf,
        existing: PathBuf,
    },

    #[error("Path '{0}' is a macOS core system protected directory and cannot be monitored")]
    SystemProtectedPath(PathBuf),
}

pub struct PathHierarchyGuard;

impl PathHierarchyGuard {
    /// List of system protected paths on macOS
    const SYSTEM_PROTECTED: &'static [&'static str] = &[
        "/",
        "/System",
        "/Library",
        "/Applications",
        "/bin",
        "/sbin",
        "/usr",
        "/private",
        "/var",
        "/etc",
        "/dev",
        "/Volumes",
    ];

    /// Check if a directory can safely be added without hierarchy conflicts or violating system protection.
    pub fn check_can_add(new_path: &Path, existing_paths: &[PathBuf]) -> Result<PathBuf, GuardError> {
        if !new_path.exists() {
            return Err(GuardError::PathNotFound(new_path.to_path_buf()));
        }

        let canonical_new = new_path
            .canonicalize()
            .map_err(|_| GuardError::PathNotFound(new_path.to_path_buf()))?;

        if !canonical_new.is_dir() {
            return Err(GuardError::NotADirectory(canonical_new));
        }

        // 1. Check system protected paths
        let path_str = canonical_new.to_string_lossy();
        for protected in Self::SYSTEM_PROTECTED {
            if path_str == *protected {
                return Err(GuardError::SystemProtectedPath(canonical_new));
            }
        }

        // Check if path is home directory itself (e.g. /Users/aubreyhan directly)
        if let Some(home) = dirs::home_dir() {
            if let Ok(canonical_home) = home.canonicalize() {
                if canonical_new == canonical_home {
                    return Err(GuardError::SystemProtectedPath(canonical_new));
                }
            }
        }

        // 2. Check overlap against existing monitored directories
        for existing in existing_paths {
            let canonical_existing = match existing.canonicalize() {
                Ok(p) => p,
                Err(_) => continue, // if existing path was deleted on disk, skip
            };

            // Case A: Identical path
            if canonical_new == canonical_existing {
                return Err(GuardError::AlreadyAdded(canonical_new));
            }

            // Case B: new_path is a child/subfolder of existing (e.g. existing: /a/b, new: /a/b/c)
            if canonical_new.starts_with(&canonical_existing) {
                return Err(GuardError::ChildOfExisting {
                    new_path: canonical_new,
                    existing: canonical_existing,
                });
            }

            // Case C: new_path is a parent of existing (e.g. existing: /a/b/c, new: /a/b)
            if canonical_existing.starts_with(&canonical_new) {
                return Err(GuardError::ParentOfExisting {
                    new_path: canonical_new,
                    existing: canonical_existing,
                });
            }
        }

        Ok(canonical_new)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn test_system_protected_paths() {
        let root = Path::new("/");
        assert!(matches!(
            PathHierarchyGuard::check_can_add(root, &[]),
            Err(GuardError::SystemProtectedPath(_))
        ));

        let sys = Path::new("/System");
        if sys.exists() {
            assert!(matches!(
                PathHierarchyGuard::check_can_add(sys, &[]),
                Err(GuardError::SystemProtectedPath(_))
            ));
        }
    }

    #[test]
    fn test_parent_and_child_overlap() {
        let temp_dir = std::env::temp_dir().join("file_purger_guard_test");
        let parent = temp_dir.join("parent");
        let child = parent.join("child");
        let sibling = temp_dir.join("sibling");

        fs::create_dir_all(&child).unwrap();
        fs::create_dir_all(&sibling).unwrap();

        let canonical_parent = parent.canonicalize().unwrap();
        let canonical_child = child.canonicalize().unwrap();
        let canonical_sibling = sibling.canonicalize().unwrap();

        // Adding child when parent exists -> Should fail (ChildOfExisting)
        let res = PathHierarchyGuard::check_can_add(&child, &[canonical_parent.clone()]);
        assert_eq!(
            res,
            Err(GuardError::ChildOfExisting {
                new_path: canonical_child.clone(),
                existing: canonical_parent.clone(),
            })
        );

        // Adding parent when child exists -> Should fail (ParentOfExisting)
        let res = PathHierarchyGuard::check_can_add(&parent, &[canonical_child.clone()]);
        assert_eq!(
            res,
            Err(GuardError::ParentOfExisting {
                new_path: canonical_parent.clone(),
                existing: canonical_child.clone(),
            })
        );

        // Adding identical path -> Should fail (AlreadyAdded)
        let res = PathHierarchyGuard::check_can_add(&parent, &[canonical_parent.clone()]);
        assert_eq!(res, Err(GuardError::AlreadyAdded(canonical_parent)));

        // Adding sibling -> Should succeed
        let res = PathHierarchyGuard::check_can_add(&sibling, &[canonical_child]);
        assert_eq!(res, Ok(canonical_sibling));

        let _ = fs::remove_dir_all(&temp_dir);
    }
}
