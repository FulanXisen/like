use async_openai::{
    config::OpenAIConfig,
    types::{
        ChatCompletionRequestMessage, ChatCompletionRequestUserMessageArgs,
        CreateChatCompletionRequestArgs,
    },
    Client,
};
use dotenv::dotenv;
use futures::StreamExt as _;
use std::env;

/// 初始化DeepSeek客户端
pub fn init_deepseek_client() -> Client<OpenAIConfig> {
    dotenv().ok(); // 加载.env文件

    let vars = env::vars();
    vars.for_each(|(k, v)| println!("{}={}", k, v));
    let config = OpenAIConfig::new()
        .with_api_base(
            env::var("DEEPSEEK_API_BASE").unwrap_or("https://api.deepseek.com/v1".into()),
        )
        .with_api_key(env::var("DEEPSEEK_API_KEY").expect("必须设置DEEPSEEK_API_KEY环境变量"));

    Client::with_config(config)
}

/// 获取可用的DeepSeek模型
pub async fn list_models(client: &Client<OpenAIConfig>) -> anyhow::Result<Vec<String>> {
    let models = client.models().list().await?;
    Ok(models
        .data
        .into_iter()
        .filter(|m| m.id.starts_with("deepseek-"))
        .map(|m| m.id)
        .collect())
}

/// 生成LeetCode题解
pub async fn generate_leetcode_solution(
    client: &Client<OpenAIConfig>,
    problem_title: &str,
    problem_desc: &str,
    language: &str,
) -> anyhow::Result<String> {
    // 构造提示词
    let user_message = ChatCompletionRequestUserMessageArgs::default()
        .content(format!(
            "你是一个算法专家，请用{}解决LeetCode题目：{}\n\n题目描述：{}\n\n要求：\n1. 包含详细注释\n2. 分析时间/空间复杂度\n3. 使用规范的代码风格",
            language, problem_title, problem_desc
        ))
        .build()?;

    // 构建请求
    let request = CreateChatCompletionRequestArgs::default()
        .model("deepseek-chat")
        .max_tokens(1024 as u32)
        .temperature(0.3)
        .messages(vec![ChatCompletionRequestMessage::User(user_message)])
        .build()?;

    // 发送请求
    let response = client.chat().create(request).await?;

    // 提取内容
    response.choices[0]
        .message
        .content
        .clone()
        .ok_or_else(|| anyhow::anyhow!("未生成有效内容"))
}

/// 流式生成（适合长代码）
pub async fn generate_leetcode_solution_stream(
    client: &Client<OpenAIConfig>,
    problem_title: &str,
    problem_desc: &str,
    language: &str,
) -> anyhow::Result<()> {
    let user_message = ChatCompletionRequestUserMessageArgs::default()
    .content(format!(
        "你是一个算法专家，请用{}解决LeetCode题目：{}\n\n题目描述：{}\n\n要求：\n1. 包含详细注释\n2. 分析时间/空间复杂度\n3. 使用规范的代码风格",
        language, problem_title, problem_desc
    ))
        .build()?;

    let request = CreateChatCompletionRequestArgs::default()
        .model("deepseek-chat")
        .stream(true)
        .messages(vec![ChatCompletionRequestMessage::User(user_message)])
        .build()?;

    let mut stream = client.chat().create_stream(request).await?;

    while let Some(response) = stream.next().await {
        match response {
            Ok(res) => {
                if let Some(content) = &res.choices[0].delta.content {
                    print!("{}", content);
                }
            }
            Err(e) => eprintln!("Error: {}", e),
        }
    }

    Ok(())
}
