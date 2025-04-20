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
use std::{env, sync::mpsc};


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

// pub async fn generate_response_stream(
//     client: &Client<OpenAIConfig>,
//     prompt: &str,
//     sender: mpsc::Sender<String>,
// ) -> anyhow::Result<()> {
//     let user_message = ChatCompletionRequestUserMessageArgs::default()
//         .content(prompt)
//         .build()?;

//     let request = CreateChatCompletionRequestArgs::default()
//         .model("deepseek-chat")
//         .stream(true)
//         .messages(vec![ChatCompletionRequestMessage::User(user_message)])
//         .build()?;

//     let mut stream = client.chat().create_stream(request).await?;

//     while let Some(response) = stream.next().await {
//         match response {
//             Ok(res) => {
//                 if let Some(content) = &res.choices[0].delta.content {
//                     print!("{}", content);
//                     sender.send(content.clone())?;
//                 }
//             }
//             Err(e) => eprintln!("Error: {}", e),
//         }
//     }

//     Ok(())
// }



pub async fn generate_response_stream(
    client: &Client<OpenAIConfig>,
    prompt: &str,
    sender: mpsc::Sender<String>,
) -> anyhow::Result<()> {
    let user_message = ChatCompletionRequestUserMessageArgs::default()
        .content(prompt)
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
                    sender.send(content.clone())?;
                }
            }
            Err(e) => eprintln!("Error: {}", e),
        }
    }

    Ok(())
}
