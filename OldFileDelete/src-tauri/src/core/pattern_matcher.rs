use globset::{GlobBuilder, GlobSet, GlobSetBuilder};
use std::path::Path;

#[derive(Debug, Clone)]
pub struct PatternMatcher {
    glob_set: Option<GlobSet>,
    stem_end_patterns: Vec<String>,
}

impl PatternMatcher {
    /// Parse comma-separated (supporting both `,` and Chinese `，`) patterns.
    /// Case-sensitive matching.
    pub fn new(patterns_str: &str) -> Result<Self, globset::Error> {
        let mut builder = GlobSetBuilder::new();
        let mut stem_end_patterns = Vec::new();
        let mut has_globs = false;

        // Split by both ASCII ',' and full-width '，'
        for raw_part in patterns_str.split(|c| c == ',' || c == '，') {
            let part = raw_part.trim();
            if part.is_empty() {
                continue;
            }

            // Check if pattern is '*suffix' where suffix has no slashes, dots or other wildcards
            // As per SPEC.md: `*a` matches files whose base stem (excluding extension) ends with `a`
            if part.starts_with('*') && part.len() > 1 {
                let suffix = &part[1..];
                if !suffix.contains('/') && !suffix.contains('.') && !suffix.contains('*') && !suffix.contains('?') {
                    stem_end_patterns.push(suffix.to_string());
                    continue;
                }
            }

            // Standard glob matching (case sensitive)
            let glob = GlobBuilder::new(part)
                .case_insensitive(false)
                .literal_separator(part.contains('/'))
                .build()?;

            builder.add(glob);
            has_globs = true;
        }

        let glob_set = if has_globs {
            Some(builder.build()?)
        } else {
            None
        };

        Ok(Self {
            glob_set,
            stem_end_patterns,
        })
    }

    /// Check if a relative path and file name matches any of the configured patterns.
    pub fn is_match(&self, rel_path: &Path, file_name: &str) -> bool {
        // 1. Check stem-ending patterns (e.g. `*a` matching `banana.txt` because stem `banana` ends with `a`)
        if !self.stem_end_patterns.is_empty() {
            let stem = Path::new(file_name)
                .file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or(file_name);

            for suffix in &self.stem_end_patterns {
                if stem.ends_with(suffix) {
                    return true;
                }
            }
        }

        // 2. Check GlobSet
        if let Some(ref set) = self.glob_set {
            // Check against full relative path (e.g. "docs/report.pdf")
            if set.is_match(rel_path) {
                return true;
            }
            // Check against just the file name (e.g. "report.pdf")
            if set.is_match(Path::new(file_name)) {
                return true;
            }
        }

        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_case_sensitivity() {
        let matcher = PatternMatcher::new("*.dmg").unwrap();
        assert!(matcher.is_match(Path::new("test.dmg"), "test.dmg"));
        assert!(!matcher.is_match(Path::new("test.DMG"), "test.DMG"));
    }

    #[test]
    fn test_multiple_patterns_with_commas() {
        let matcher = PatternMatcher::new("*.dmg, *.pkg，**/archive*").unwrap();
        assert!(matcher.is_match(Path::new("app.dmg"), "app.dmg"));
        assert!(matcher.is_match(Path::new("setup.pkg"), "setup.pkg"));
        assert!(matcher.is_match(Path::new("sub/dir/archive_2024.zip"), "archive_2024.zip"));
        assert!(!matcher.is_match(Path::new("test.txt"), "test.txt"));
    }

    #[test]
    fn test_stem_ending_pattern() {
        // `*a` matches files whose base stem (excluding extension) ends with `a`
        let matcher = PatternMatcher::new("*a").unwrap();
        assert!(matcher.is_match(Path::new("banana.txt"), "banana.txt"));
        assert!(matcher.is_match(Path::new("banana.doc"), "banana.doc"));
        assert!(matcher.is_match(Path::new("data.log"), "data.log"));
        assert!(!matcher.is_match(Path::new("apple.txt"), "apple.txt"));
        assert!(!matcher.is_match(Path::new("orange.pdf"), "orange.pdf"));
    }

    #[test]
    fn test_prefix_and_extension_pattern() {
        let matcher = PatternMatcher::new("a*.j*").unwrap();
        assert!(matcher.is_match(Path::new("app.jpeg"), "app.jpeg"));
        assert!(matcher.is_match(Path::new("apple.jpg"), "apple.jpg"));
        assert!(!matcher.is_match(Path::new("banana.jpg"), "banana.jpg"));
    }

    #[test]
    fn test_recursive_pattern() {
        let matcher = PatternMatcher::new("**/*.dmg").unwrap();
        assert!(matcher.is_match(Path::new("a/b/c/setup.dmg"), "setup.dmg"));
        assert!(matcher.is_match(Path::new("setup.dmg"), "setup.dmg"));
    }
}
