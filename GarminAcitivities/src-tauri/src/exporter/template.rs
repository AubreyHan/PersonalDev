use regex::Regex;

pub const DEFAULT_NAMING_TEMPLATE: &str = "{date}_{type}_{name}_{id}";

pub struct TemplateContext<'a> {
    pub activity_id: i64,
    pub activity_name: &'a str,
    pub activity_type: &'a str,
    pub start_time_local: &'a str,
    pub distance_meters: f64,
}

pub struct TemplateEngine;

impl TemplateEngine {
    /// 跨平台文件名非法字符净化：过滤 `\ / : * ? " < > |` 及换行控制符，转换为 `_`
    pub fn sanitize_filename(name: &str) -> String {
        let re_illegal = Regex::new(r#"[\\/:*?"<>|\r\n\t]+"#).unwrap();
        let replaced = re_illegal.replace_all(name, "_");
        
        // 合并连续的空白与下划线，例如 " _ " 或 "__" 压缩为单个 "_"
        let re_cleanup_underscores = Regex::new(r"(\s*_\s*)+").unwrap();
        let cleaned = re_cleanup_underscores.replace_all(&replaced, "_");

        let re_spaces = Regex::new(r"[ \t]+").unwrap();
        let trimmed_spaces = re_spaces.replace_all(&cleaned, " ");

        let trimmed = trimmed_spaces.trim_matches(|c| c == ' ' || c == '_' || c == '.');
        if trimmed.is_empty() {
            "untitled_activity".to_string()
        } else {
            trimmed.to_string()
        }
    }

    /// 运动类型英文至中文友好描述映射
    pub fn localize_activity_type(type_en: &str) -> &'static str {
        match type_en.to_lowercase().as_str() {
            "running" | "trail_running" | "treadmill_running" => "跑步",
            "cycling" | "road_biking" | "mountain_biking" | "gravel_cycling" => "骑行",
            "swimming" | "lap_swimming" | "open_water_swimming" => "游泳",
            "hiking" => "徒步",
            "walking" => "健走",
            "fitness_equipment" | "strength_training" => "力量训练",
            "yoga" => "瑜伽",
            "skiing" | "snowboarding" => "滑雪",
            _ => "运动",
        }
    }

    /// 渲染格式化文件名（不含扩展名）
    pub fn render(template: &str, ctx: &TemplateContext) -> String {
        let tpl = if template.trim().is_empty() {
            DEFAULT_NAMING_TEMPLATE
        } else {
            template
        };

        // 提取日期与时间 YYYY-MM-DD HH:MM:SS
        let (date, compact_date, time) = if ctx.start_time_local.len() >= 10 {
            let d = &ctx.start_time_local[0..10];
            let cd = d.replace('-', "");
            let t = if ctx.start_time_local.len() >= 16 {
                let hh = &ctx.start_time_local[11..13];
                let mm = &ctx.start_time_local[14..16];
                format!("{}{}", hh, mm)
            } else {
                "0000".to_string()
            };
            (d.to_string(), cd, t)
        } else {
            ("1970-01-01".to_string(), "19700101".to_string(), "0000".to_string())
        };

        let sanitized_name = Self::sanitize_filename(ctx.activity_name);
        let localized_type = Self::localize_activity_type(ctx.activity_type);
        let dist_km = format!("{:.1}km", ctx.distance_meters / 1000.0);

        let mut result = tpl.to_string();
        result = result.replace("{date}", &date);
        result = result.replace("{compact_date}", &compact_date);
        result = result.replace("{time}", &time);
        result = result.replace("{id}", &ctx.activity_id.to_string());
        result = result.replace("{type}", localized_type);
        result = result.replace("{type_en}", ctx.activity_type);
        result = result.replace("{name}", &sanitized_name);
        result = result.replace("{dist_km}", &dist_km);

        Self::sanitize_filename(&result)
    }

    /// 组合生成最终完整文件名（附带扩展名）
    pub fn build_filename(template: &str, ctx: &TemplateContext, format: &str) -> String {
        let base_name = Self::render(template, ctx);
        let ext = format.trim_start_matches('.').to_lowercase();
        format!("{}.{}", base_name, ext)
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    #[test]
    fn test_sanitize_filename() {
        assert_eq!(
            TemplateEngine::sanitize_filename("晨跑 / 10k *PB* ? <测试> | \"高心率\""),
            "晨跑_10k_PB_测试_高心率"
        );
        assert_eq!(TemplateEngine::sanitize_filename("///:::***"), "untitled_activity");
        assert_eq!(TemplateEngine::sanitize_filename("  Normal Run  "), "Normal Run");
    }

    #[test]
    fn test_render_template() {
        let ctx = TemplateContext {
            activity_id: 16892401234,
            activity_name: "奥森公园 / 晨跑 10K",
            activity_type: "running",
            start_time_local: "2026-11-03 06:45:12",
            distance_meters: 10240.0,
        };

        // 默认模板: {date}_{type}_{name}_{id}
        let default_rendered = TemplateEngine::render("", &ctx);
        assert_eq!(
            default_rendered,
            "2026-11-03_跑步_奥森公园_晨跑 10K_16892401234"
        );

        // 自定义紧凑模板
        let custom_tpl = "{compact_date}_{time}_{type_en}_{dist_km}_{id}";
        let custom_rendered = TemplateEngine::render(custom_tpl, &ctx);
        assert_eq!(
            custom_rendered,
            "20261103_0645_running_10.2km_16892401234"
        );

        // 完整文件名生成带扩展名
        let gpx_file = TemplateEngine::build_filename(DEFAULT_NAMING_TEMPLATE, &ctx, "gpx");
        assert_eq!(
            gpx_file,
            "2026-11-03_跑步_奥森公园_晨跑 10K_16892401234.gpx"
        );
    }
}
