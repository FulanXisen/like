use ai_code_training::problem::print_leet_code_problem;
use ai_code_training::problem::LeetCodeProblem;
use anyhow::{Context, Ok, Result};
use log::{debug, error, info, trace, warn};
use reqwest::Client;
use serde::{Deserialize, Serialize};
use std::path::Path;
use tokio::fs::{self, File};
use tokio::io::AsyncWriteExt;

// ---------- 数据结构定义 ----------
/// LeetCode GraphQL API 返回的根结构
#[derive(Debug, Serialize, Deserialize)]
struct GraphQLResponse {
    data: QuestionData,
}

/// 问题数据容器
#[derive(Debug, Serialize, Deserialize)]
struct QuestionData {
    question: Question,
}

/// 题目详细信息
#[derive(Debug, Serialize, Deserialize)]
struct Question {
    title: String,
    content: String,
    difficulty: String,
    #[serde(rename = "topicTags")]
    topic_tags: Vec<Tag>,
    #[serde(rename = "codeSnippets")]
    code_snippets: Vec<CodeSnippet>,
}

/// 题目分类标签
#[derive(Debug, Serialize, Deserialize)]
struct Tag {
    name: String,
}

/// 代码片段（支持多语言）
#[derive(Debug, Serialize, Deserialize)]
struct CodeSnippet {
    lang: String,
    #[serde(rename = "langSlug")]
    lang_slug: String,
    code: String,
}

// ---------- 常量定义 ----------
const CACHE_DIR: &str = ".leetcode_cache";
const LEETCODE_API: &str = "https://leetcode.com/graphql";

use ai_code_training::code_gen::*;
#[tokio::main]
async fn main() -> Result<()> {
    init_logger();
    trace!("初始化DeepSeek客户端");
    let client = init_deepseek_client();
    // 示例：生成Two Sum的Rust解法
    let solution = generate_leetcode_solution_stream(
        &client,
        "Two Sum",
        "给定一个整数数组nums和一个目标值target...",
        "rust",
    )
    .await?;

    // println!("生成的解法:\n{}", solution);
    return Ok(());
}
// ---------- 主函数 ----------
// #[tokio::main]
// async fn main() -> Result<()> {
//     // 初始化 env_logger 日志（开发环境用全量 TRACE 级别）
//     init_logger();

//     let problem_id = "215";
//     // 示例调用（带错误处理演示）
//     match get_leetcode_problem(problem_id).await {
//         Ok(problem) => {
//             info!("成功获取题目: {}", problem.title);
//             // debug!("完整数据: {:?}", problem);
//             print_leet_code_problem(&problem);
//         }
//         Err(e) => {
//             error!("获取题目失败: {}", e);
//         }
//     }
//     Ok(())
// }

/// 初始化 env_logger 配置
fn init_logger() {
    use env_logger::{Builder, Target};
    use std::env;

    Builder::new()
        // 设置默认日志级别为 TRACE
        .filter_level(log::LevelFilter::Trace)
        // 开发环境友好格式（带颜色）
        .format_timestamp_nanos()
        .format_module_path(true)
        .format_level(true)
        .target(Target::Stdout)
        // 允许通过 RUST_LOG 环境变量覆盖配置
        .parse_env("RUST_LOG")
        .init();

    info!("logger initialized (TRACE level)");
}

// ---------- 核心逻辑 ----------
/// 获取 LeetCode 题目数据（带缓存和详细日志）
pub async fn get_leetcode_problem(problem_id: &str) -> Result<LeetCodeProblem> {
    trace!("开始获取题目数据: problem_id={}", problem_id);

    let cache_path = format!("{}/{}.json", CACHE_DIR, problem_id);

    // ---------- 缓存读取阶段 ----------
    trace!("检查本地缓存: path={}", cache_path);
    if Path::new(&cache_path).exists() {
        trace!("缓存存在，尝试读取");
        let cached_data = fs::read_to_string(&cache_path)
            .await
            .context("读取缓存文件失败")?;

        trace!("解析缓存JSON数据");
        let problem: LeetCodeProblem =
            serde_json::from_str(&cached_data).context("解析缓存数据失败")?;

        debug!("命中缓存: title={}", problem.title);
        return Ok(problem);
    }
    trace!("无有效缓存");

    // ---------- API 请求阶段 ----------
    trace!("准备 GraphQL 请求");
    let client = Client::new();
    let title_slug = id_to_slug(problem_id)?;
    trace!("转换后的题目slug: {}", title_slug);

    let query = r#"
    query getQuestionDetail($titleSlug: String!) {
        question(titleSlug: $titleSlug) {
            title
            content
            difficulty
            topicTags { name }
            codeSnippets { lang langSlug code }
        }
    }
    "#;

    let request_body = serde_json::json!({
        "query": query,
        "variables": {"titleSlug": title_slug}
    });

    trace!("发送 GraphQL 请求");
    let resp = client
        .post(LEETCODE_API)
        .json(&request_body)
        .send()
        .await
        .context("API 请求失败")?;

    trace!("收到API响应: status={}", resp.status());
    if !resp.status().is_success() {
        let status = resp.status();
        let body = resp.text().await.unwrap_or_default();
        error!("API 返回错误: status={}, body={}", status, body);
        anyhow::bail!("API 错误: {}", status);
    }

    // ---------- 响应处理阶段 ----------
    trace!("解析JSON响应体");
    let api_response: GraphQLResponse = resp.json().await.context("解析JSON失败")?;
    let question = api_response.data.question;
    debug!("获取到原始数据: title={}", question.title);

    // 清理 HTML 描述
    trace!("清理HTML格式的描述");
    let description = html2text::from_read(question.content.as_bytes(), 80)
        .trim()
        .to_string();

    // 构建代码片段映射表
    trace!("处理多语言代码片段");
    let mut code_snippets = std::collections::HashMap::new();
    for snippet in question.code_snippets {
        trace!("处理代码片段: lang={}", snippet.lang_slug);
        code_snippets.insert(snippet.lang_slug, snippet.code);
    }

    let problem = LeetCodeProblem {
        title: question.title,
        description,
        difficulty: question.difficulty,
        tags: question.topic_tags.into_iter().map(|t| t.name).collect(),
        code_snippets,
    };

    // ---------- 缓存写入阶段 ----------
    trace!("写入缓存文件");
    fs::create_dir_all(CACHE_DIR)
        .await
        .context("创建缓存目录失败")?;

    let mut file = File::create(&cache_path)
        .await
        .context("创建缓存文件失败")?;

    let json_data = serde_json::to_string_pretty(&problem).context("序列化缓存数据失败")?;

    file.write_all(json_data.as_bytes())
        .await
        .context("写入缓存失败")?;

    info!(
        "成功获取并缓存题目: title={}, difficulty={}",
        problem.title, problem.difficulty
    );
    Ok(problem)
}

/// 将问题 ID 转换为 title_slug（带错误日志）
fn id_to_slug(problem_id: &str) -> Result<String> {
    trace!("开始ID转换: problem_id={}", problem_id);

    // 实际项目中这里应该从文件/数据库加载完整映射
    let id_map = [
        ("1", "two-sum"),
        ("215", "kth-largest-element-in-an-array"),
        ("3", "longest-substring-with-repeating-characters"),
    ];

    id_map
        .iter()
        .find(|(id, _)| *id == problem_id)
        .map(|(_, slug)| {
            trace!("找到匹配的slug: {}", slug);
            slug.to_string()
        })
        .ok_or_else(|| {
            error!("未知的问题ID: {}", problem_id);
            anyhow::anyhow!("未知的问题ID: {}", problem_id)
        })
}
