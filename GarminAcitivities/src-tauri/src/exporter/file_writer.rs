use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum FileWriterError {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Export directory does not exist or is invalid: {0}")]
    InvalidDirectory(String),
}

pub struct SavedFileInfo {
    pub file_path: PathBuf,
    pub file_size_bytes: i64,
    pub checksum_md5: Option<String>,
}

pub struct FileWriter;

impl FileWriter {
    /// 直接平铺写入目标根目录，彻底避免创建嵌套子目录
    pub fn save_flat_file(
        target_dir: &Path,
        filename: &str,
        content: &[u8],
        override_existing: bool,
    ) -> Result<SavedFileInfo, FileWriterError> {
        if !target_dir.exists() {
            std::fs::create_dir_all(target_dir)?;
        }

        let mut final_path = target_dir.join(filename);

        // 若不允许覆盖且文件已存在，添加序号后缀 (1), (2)
        if !override_existing && final_path.exists() {
            let stem = final_path
                .file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or("activity");
            let ext = final_path
                .extension()
                .and_then(|s| s.to_str())
                .unwrap_or("gpx");

            let mut counter = 1;
            loop {
                let candidate = target_dir.join(format!("{}_{}.{}", stem, counter, ext));
                if !candidate.exists() {
                    final_path = candidate;
                    break;
                }
                counter += 1;
            }
        }

        let mut file = File::create(&final_path)?;
        file.write_all(content)?;
        file.flush()?;

        let file_size_bytes = content.len() as i64;
        // 计算简单 MD5 校验和作为一致性标识
        let checksum_md5 = Some(format!("{:x}", md5_digest(content)));

        Ok(SavedFileInfo {
            file_path: final_path,
            file_size_bytes,
            checksum_md5,
        })
    }
}

fn md5_digest(data: &[u8]) -> u128 {
    // 简易 128-bit FNV-1a 哈希替代外部大型 MD5 库，保证跨平台轻量无外部 C 依赖
    let mut hash: u128 = 0x6c62272e07bb014262b821756295c58d;
    for &byte in data {
        hash ^= byte as u128;
        hash = hash.wrapping_mul(0x1000000000000000000013b);
    }
    hash
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_flat_file_save() {
        let temp_dir = std::env::temp_dir().join("garmin_test_flat_export");
        let filename = "2026-11-03_跑步_测试_123456.gpx";
        let mock_gpx = b"<?xml version=\"1.0\" encoding=\"UTF-8\"?><gpx></gpx>";

        let info = FileWriter::save_flat_file(&temp_dir, filename, mock_gpx, true).unwrap();
        assert!(info.file_path.exists());
        assert_eq!(info.file_size_bytes, mock_gpx.len() as i64);
        assert!(info.checksum_md5.is_some());

        // 清理测试临时文件
        let _ = std::fs::remove_file(info.file_path);
        let _ = std::fs::remove_dir(&temp_dir);
    }
}
