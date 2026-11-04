use regex::Regex;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum MergerError {
    #[error("No valid track points found in provided files")]
    EmptyTracks,
    #[error("XML parsing error: {0}")]
    Xml(String),
    #[error("Unsupported format for merging: {0}")]
    UnsupportedFormat(String),
}

pub struct TrackMerger;

impl TrackMerger {
    /// 无损合并多个 GPX 轨迹为单一标准 GPX 文件
    pub fn merge_gpx(gpx_contents: &[&str], merged_title: &str) -> Result<String, MergerError> {
        if gpx_contents.is_empty() {
            return Err(MergerError::EmptyTracks);
        }

        let re_trkseg = Regex::new(r"(?s)<trkseg>(.*?)</trkseg>").unwrap();
        let re_trkpt = Regex::new(r"(?s)<trkpt\s+[^>]*>.*?</trkpt>").unwrap();
        let re_time = Regex::new(r"<time>(.*?)</time>").unwrap();

        let mut all_segments: Vec<(String, String)> = Vec::new(); // (first_point_time, segment_content)

        for content in gpx_contents {
            for cap in re_trkseg.captures_iter(content) {
                let seg_inner = cap.get(1).map(|m| m.as_str()).unwrap_or("");
                if let Some(first_pt) = re_trkpt.find(seg_inner) {
                    let first_pt_str = first_pt.as_str();
                    let timestamp = re_time
                        .captures(first_pt_str)
                        .and_then(|c| c.get(1))
                        .map(|m| m.as_str())
                        .unwrap_or("")
                        .to_string();
                    all_segments.push((timestamp, seg_inner.to_string()));
                }
            }
        }

        if all_segments.is_empty() {
            return Err(MergerError::EmptyTracks);
        }

        // 按时间升序严格排列各个分段
        all_segments.sort_by(|a, b| a.0.cmp(&b.0));

        let first_timestamp = all_segments
            .first()
            .map(|s| s.0.as_str())
            .unwrap_or("2026-11-03T00:00:00Z");

        let mut output = String::with_capacity(1024 * 128);
        output.push_str("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        output.push_str("<gpx creator=\"Garmin Activity Exporter\" version=\"1.1\"\n");
        output.push_str("  xmlns=\"http://www.topografix.com/GPX/1/1\"\n");
        output.push_str("  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
        output.push_str("  xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd http://www.garmin.com/xmlschemas/TrackPointExtension/v1 http://www.garmin.com/xmlschemas/TrackPointExtensionv1.xsd\"\n");
        output.push_str("  xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\">\n");
        output.push_str("  <metadata>\n");
        output.push_str(&format!("    <name>{}</name>\n", merged_title));
        output.push_str(&format!("    <time>{}</time>\n", first_timestamp));
        output.push_str("  </metadata>\n");
        output.push_str("  <trk>\n");
        output.push_str(&format!("    <name>{}</name>\n", merged_title));

        for (_, seg_content) in all_segments {
            output.push_str("    <trkseg>\n");
            output.push_str(&seg_content);
            output.push_str("\n    </trkseg>\n");
        }

        output.push_str("  </trk>\n");
        output.push_str("</gpx>\n");

        Ok(output)
    }

    /// 无损合并多个 TCX 轨迹为单一标准 TCX 文件
    pub fn merge_tcx(tcx_contents: &[&str], sport: &str) -> Result<String, MergerError> {
        if tcx_contents.is_empty() {
            return Err(MergerError::EmptyTracks);
        }

        let re_lap = Regex::new(r"(?s)<Lap\s+[^>]*>.*?</Lap>").unwrap();
        let re_lap_time = Regex::new(r#"StartTime=["']([^"']+)["']"#).unwrap();

        let mut all_laps: Vec<(String, String)> = Vec::new();

        for content in tcx_contents {
            for cap in re_lap.captures_iter(content) {
                let lap_xml = cap.get(0).map(|m| m.as_str()).unwrap_or("");
                let start_time = re_lap_time
                    .captures(lap_xml)
                    .and_then(|c| c.get(1))
                    .map(|m| m.as_str())
                    .unwrap_or("")
                    .to_string();
                all_laps.push((start_time, lap_xml.to_string()));
            }
        }

        if all_laps.is_empty() {
            return Err(MergerError::EmptyTracks);
        }

        // 按时间升序排序
        all_laps.sort_by(|a, b| a.0.cmp(&b.0));

        let initial_id = all_laps.first().map(|l| l.0.as_str()).unwrap_or("2026-11-03T00:00:00Z");

        let mut output = String::with_capacity(1024 * 128);
        output.push_str("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        output.push_str("<TrainingCenterDatabase\n");
        output.push_str("  xmlns=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2\"\n");
        output.push_str("  xmlns:ns2=\"http://www.garmin.com/xmlschemas/UserProfile/v2\"\n");
        output.push_str("  xmlns:ns3=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\n");
        output.push_str("  <Activities>\n");
        output.push_str(&format!("    <Activity Sport=\"{}\">\n", sport));
        output.push_str(&format!("      <Id>{}</Id>\n", initial_id));

        for (_, lap_content) in all_laps {
            output.push_str("      ");
            output.push_str(&lap_content);
            output.push_str("\n");
        }

        output.push_str("    </Activity>\n");
        output.push_str("  </Activities>\n");
        output.push_str("</TrainingCenterDatabase>\n");

        Ok(output)
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    #[test]
    fn test_merge_gpx_lossless() {
        let gpx_part2 = r#"<?xml version="1.0" encoding="UTF-8"?>
        <gpx version="1.1" xmlns="http://www.topografix.com/GPX/1/1" xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1">
          <trk>
            <trkseg>
              <trkpt lat="39.91" lon="116.41">
                <ele>48.0</ele>
                <time>2026-11-03T08:00:00Z</time>
                <extensions>
                  <gpxtpx:TrackPointExtension>
                    <gpxtpx:hr>162</gpxtpx:hr>
                    <gpxtpx:cad>185</gpxtpx:cad>
                  </gpxtpx:TrackPointExtension>
                </extensions>
              </trkpt>
            </trkseg>
          </trk>
        </gpx>"#;

        let gpx_part1 = r#"<?xml version="1.0" encoding="UTF-8"?>
        <gpx version="1.1" xmlns="http://www.topografix.com/GPX/1/1" xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1">
          <trk>
            <trkseg>
              <trkpt lat="39.90" lon="116.40">
                <ele>45.0</ele>
                <time>2026-11-03T07:30:00Z</time>
                <extensions>
                  <gpxtpx:TrackPointExtension>
                    <gpxtpx:hr>155</gpxtpx:hr>
                    <gpxtpx:cad>180</gpxtpx:cad>
                  </gpxtpx:TrackPointExtension>
                </extensions>
              </trkpt>
            </trkseg>
          </trk>
        </gpx>"#;

        // 故意乱序传入 (part2, part1)，验证是否按时间升序对齐并保持心率/踏频
        let merged = TrackMerger::merge_gpx(&[gpx_part2, gpx_part1], "合并晨跑").unwrap();

        assert!(merged.contains("<name>合并晨跑</name>"));
        assert!(merged.contains("<gpxtpx:hr>155</gpxtpx:hr>"));
        assert!(merged.contains("<gpxtpx:hr>162</gpxtpx:hr>"));
        assert!(merged.contains("<gpxtpx:cad>185</gpxtpx:cad>"));

        // 验证时序升序 (07:30:00 必须出现在 08:00:00 前面)
        let pos1 = merged.find("2026-11-03T07:30:00Z").unwrap();
        let pos2 = merged.find("2026-11-03T08:00:00Z").unwrap();
        assert!(pos1 < pos2);
    }

    #[test]
    fn test_merge_tcx() {
        let tcx_part1 = r#"<TrainingCenterDatabase>
          <Activities>
            <Activity Sport="Running">
              <Lap StartTime="2026-11-03T07:00:00Z">
                <TotalTimeSeconds>1200</TotalTimeSeconds>
              </Lap>
            </Activity>
          </Activities>
        </TrainingCenterDatabase>"#;

        let tcx_part2 = r#"<TrainingCenterDatabase>
          <Activities>
            <Activity Sport="Running">
              <Lap StartTime="2026-11-03T07:25:00Z">
                <TotalTimeSeconds>1500</TotalTimeSeconds>
              </Lap>
            </Activity>
          </Activities>
        </TrainingCenterDatabase>"#;

        let merged = TrackMerger::merge_tcx(&[tcx_part1, tcx_part2], "Running").unwrap();
        assert!(merged.contains("Sport=\"Running\""));
        assert!(merged.contains("<TotalTimeSeconds>1200</TotalTimeSeconds>"));
        assert!(merged.contains("<TotalTimeSeconds>1500</TotalTimeSeconds>"));
    }
}
