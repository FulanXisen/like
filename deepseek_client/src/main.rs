use std::sync;

use async_openai::{
    config::OpenAIConfig,
    types::{
        ChatCompletionRequestAssistantMessageArgs, ChatCompletionRequestMessage,
        ChatCompletionRequestUserMessageArgs, CreateChatCompletionRequestArgs,
    },
    Client,
};
use dotenv::dotenv;
use eframe::egui;
use egui::ahash::HashMap;
use futures::{channel::mpsc, StreamExt as _};
use log::{info, trace};
use poll_promise::Promise;
use serde::{Deserialize, Serialize}; // Add this for serialization
use serde_json;
use std::env;
use uuid::Uuid; // Add this import for generating unique IDs // Add this for JSON handling

#[tokio::main]
async fn main() -> Result<(), eframe::Error> {
    init_logger();
    eframe::run_native(
        "DeepSeek Chat Client",
        eframe::NativeOptions::default(),
        Box::new(|_cc| Ok(Box::new(ChatApp::default()))), // Wrap in `Ok` to match the expected type
    )
}

fn init_logger() {
    use env_logger::{Builder, Target};

    Builder::new()
        // 设置默认日志级别为 TRACE
        .filter_level(log::LevelFilter::Info)
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
#[derive(Debug, Clone, Serialize, Deserialize)]
enum MessageSender {
    User,
    DeepSeek,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
enum MessageStatus {
    Ok,
    ErrUserAbort,
    ErrServer400,
    ErrServer401,
    ErrServer402,
    ErrServer422,
    ErrServer429,
    ErrServer500,
    ErrServer503,
}
#[derive(Debug, Clone, Serialize, Deserialize)] // Add Serialize and Deserialize
struct ChatHistory {
    uuid: Uuid, // Add a unique identifier for each chat history
    title: String,
    messages: Vec<ChatMessage>, // Store user and DeepSeek messages as `ChatMessage`
}

#[derive(Debug, Clone, Serialize, Deserialize)] // Add Serialize and Deserialize
struct ChatMessage {
    sender: MessageSender,
    content: String,
    status: MessageStatus,
}

impl Default for ChatHistory {
    fn default() -> Self {
        Self {
            uuid: Uuid::new_v4(), // Generate a new unique ID
            title: "noname".to_string(),
            messages: vec![
                ChatMessage {
                    sender: MessageSender::User,
                    content: "Hello".to_string(),
                    status: MessageStatus::Ok,
                },
                ChatMessage {
                    sender: MessageSender::DeepSeek,
                    content: "Hi there!".to_string(),
                    status: MessageStatus::Ok,
                },
            ],
        }
    }
}

struct ChatApp {
    chat_history: HashMap<Uuid, ChatHistory>,
    curr_chat: ChatHistory,
    curr_recv: sync::Arc<sync::Mutex<sync::mpsc::Receiver<String>>>,
    curr_send: sync::Arc<sync::Mutex<sync::mpsc::Sender<String>>>,
    curr_promise: Option<Promise<Result<(), anyhow::Error>>>,
    curr_input: String,
    curr_answer: String,
    should_repaint: bool,
}

impl Default for ChatApp {
    fn default() -> Self {
        let (send, recv): (sync::mpsc::Sender<String>, sync::mpsc::Receiver<String>) =
            sync::mpsc::channel();
        Self {
            chat_history: vec![
                ChatHistory {
                    uuid: Uuid::new_v4(), // Generate a unique ID for each chat
                    title: "Chat 1".to_string(),
                    messages: vec![
                        ChatMessage {
                            sender: MessageSender::User,
                            content: "Hello".to_string(),
                            status: MessageStatus::Ok,
                        },
                        ChatMessage {
                            sender: MessageSender::DeepSeek,
                            content: "Hi there!".to_string(),
                            status: MessageStatus::Ok,
                        },
                    ],
                },
                ChatHistory {
                    uuid: Uuid::new_v4(),
                    title: "Chat 2".to_string(),
                    messages: vec![
                        ChatMessage {
                            sender: MessageSender::User,
                            content: "How are you?".to_string(),
                            status: MessageStatus::Ok,
                        },
                        ChatMessage {
                            sender: MessageSender::DeepSeek,
                            content: "I'm good, thanks!".to_string(),
                            status: MessageStatus::Ok,
                        },
                    ],
                },
            ]
            .into_iter()
            .map(|chat| (chat.uuid, chat)) // Store chats in the HashMap by their UUID
            .collect(),
            curr_chat: ChatHistory::default(),
            curr_recv: sync::Arc::new(sync::Mutex::new(recv)),
            curr_send: sync::Arc::new(sync::Mutex::new(send)),
            curr_promise: None,
            curr_input: "".to_string(),
            curr_answer: "".to_string(),
            should_repaint: false,
        }
    }
}

impl ChatApp {}

// Update the `update` method to use `send_message_with_history`
impl eframe::App for ChatApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if self.should_repaint {
            ctx.request_repaint();
        }
        // Left side chat history titles
        egui::SidePanel::left("chat_history_panel")
            .resizable(false)
            .show(ctx, |ui| {
                ui.heading("Chat History");
                let uuids = self.chat_history.keys().cloned().collect::<Vec<Uuid>>();
                for uuid in uuids {
                    if ui.button(&self.chat_history[&uuid].title).clicked() {
                        // Save the current chat
                        if self.curr_chat.uuid != uuid {
                            self.chat_history
                                .insert(self.curr_chat.uuid.clone(), self.curr_chat.clone());
                        }
                        // Load the selected chat
                        self.curr_chat = self.chat_history[&uuid].clone();
                    }
                }
            });

        // Current Chat Box
        egui::TopBottomPanel::top("current_chat_title_panel").show(ctx, |ui| {
            ui.heading(&self.curr_chat.title);
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.vertical(|ui| {
                // Chat messages area
                egui::ScrollArea::vertical()
                    .max_width(ui.available_width())
                    .max_height(ui.available_height() - 120.0)
                    .auto_shrink([false; 2]) // Prevent shrinking
                    .show(ui, |ui| {
                        for message in &self.curr_chat.messages {
                            let sender_text = match message.sender {
                                MessageSender::User => "You",
                                MessageSender::DeepSeek => "DeepSeek",
                            };
                            let status_text = match message.status {
                                MessageStatus::Ok => "✔️",
                                MessageStatus::ErrUserAbort => "❌ (User Abort)",
                                MessageStatus::ErrServer400 => "❌ (400 Bad Request)",
                                MessageStatus::ErrServer401 => "❌ (401 Unauthorized)",
                                MessageStatus::ErrServer402 => "❌ (402 Payment Required)",
                                MessageStatus::ErrServer422 => "❌ (422 Unprocessable Entity)",
                                MessageStatus::ErrServer429 => "❌ (429 Too Many Requests)",
                                MessageStatus::ErrServer500 => "❌ (500 Internal Server Error)",
                                MessageStatus::ErrServer503 => "❌ (503 Service Unavailable)",
                            };
                            ui.label(format!(
                                "{}: {} [{}]",
                                sender_text, message.content, status_text
                            ));
                        }
                        if self.curr_answer != "" {
                            if self.should_repaint {
                                ui.label(format!("DeepSeek: {}\n", self.curr_answer));
                            } else {
                                self.curr_chat.messages.push(ChatMessage {
                                    sender: MessageSender::DeepSeek,
                                    content: format!("DeepSeek: {}\n", self.curr_answer),
                                    status: MessageStatus::Ok,
                                });
                                self.curr_answer.clear();
                            }
                        }
                    });

                ui.separator();
                // Adjust layout to `top_down` to avoid unnecessary space
                ui.with_layout(egui::Layout::top_down(egui::Align::Min), |ui| {
                    ui.horizontal(|ui| {
                        ui.add_sized(
                            [ui.available_width() - 80.0, 0.0], // Input box takes most of the width
                            egui::TextEdit::multiline(&mut self.curr_input)
                                .desired_rows(7)
                                .hint_text("Type your message here..."),
                        );
                        let send_button = egui::Button::new("Send");
                        let send_response = ui.add_sized(
                            [80.0 - ui.spacing().item_spacing.x, 50.0], // Send button takes the remaining width
                            send_button,
                        );
                        if send_response.clicked() {
                            if !self
                                .curr_chat
                                .messages
                                .last()
                                .unwrap()
                                .content
                                .trim()
                                .is_empty()
                            {
                                let input = self.curr_input.clone();
                                let sender_clone = sync::Arc::clone(&self.curr_send);
                                let history_clone = self.curr_chat.clone();
                                let promise = poll_promise::Promise::spawn_async(async move {
                                    deepseek_all_in_one(history_clone, input, sender_clone).await
                                });
                                // Send the message with history to the server
                                self.curr_promise = Some(promise);
                                // Push input to message history
                                self.curr_chat.messages.push(ChatMessage {
                                    sender: MessageSender::User,
                                    content: self.curr_input.clone(),
                                    status: MessageStatus::Ok,
                                });
                                // Clear the input box
                                self.curr_input.clear();

                                // repaint
                                self.should_repaint = true;
                            }
                        }
                        if let Some(promise) = &self.curr_promise {
                            if let Some(result) = promise.ready() {
                                match result {
                                    Ok(_) => {
                                        // Handle success
                                        info!("Message sent successfully");
                                    }
                                    Err(_) => {
                                        // Handle error
                                        info!("Error sending message");
                                    }
                                }
                                self.should_repaint = false;
                                self.curr_promise = None; // Clear the promise after handling
                            }
                        }
                        if let Ok(v) = self.curr_recv.lock().unwrap().try_recv() {
                            println!("Received message: {}", v);
                            self.curr_answer.push_str(&v);
                        }
                    });
                });
            });
        });
    }
}

pub async fn deepseek_all_in_one(
    chat_history: ChatHistory,
    curr_input: String,
    sender: sync::Arc<sync::Mutex<sync::mpsc::Sender<String>>>,
) -> anyhow::Result<()> {
    let client = create_deepseek_client();
    let history = pack_history(&chat_history);
    generate_response_stream(&client, history, &curr_input, sender).await?;
    return Ok(());
}

pub fn create_deepseek_client() -> Client<OpenAIConfig> {
    dotenv().ok(); // 加载.env文件
    let vars = env::vars();
    let config = OpenAIConfig::new()
        .with_api_base(
            env::var("DEEPSEEK_API_BASE").unwrap_or("https://api.deepseek.com/v1".into()),
        )
        .with_api_key(env::var("DEEPSEEK_API_KEY").expect("必须设置DEEPSEEK_API_KEY环境变量"));

    Client::with_config(config)
}

pub fn pack_history(history: &ChatHistory) -> Vec<ChatCompletionRequestMessage> {
    let messages: Vec<ChatCompletionRequestMessage> = history
        .messages
        .iter()
        .map(|msg| match msg.sender {
            MessageSender::User => {
                let args = ChatCompletionRequestUserMessageArgs::default()
                    .content(msg.content.clone())
                    .build()
                    .unwrap();
                ChatCompletionRequestMessage::User(args)
            }
            MessageSender::DeepSeek => {
                let args = ChatCompletionRequestAssistantMessageArgs::default()
                    .content(msg.content.clone())
                    .build()
                    .unwrap();

                ChatCompletionRequestMessage::Assistant(args)
            }
        })
        .collect();
    return messages;
}

pub async fn generate_response_stream(
    client: &Client<OpenAIConfig>,
    mut history: Vec<ChatCompletionRequestMessage>,
    prompt: &str,
    sender: sync::Arc<sync::Mutex<sync::mpsc::Sender<String>>>,
) -> anyhow::Result<()> {
    let user_message = ChatCompletionRequestUserMessageArgs::default()
        .content(prompt)
        .build()?;

    history.push(ChatCompletionRequestMessage::User(user_message));
    let request = CreateChatCompletionRequestArgs::default()
        .model("deepseek-chat")
        .stream(true)
        .messages(history)
        .build()?;

    let mut stream = client.chat().create_stream(request).await?;

    while let Some(response) = stream.next().await {
        match response {
            Ok(res) => {
                if let Some(content) = &res.choices[0].delta.content {
                    print!("{}", content);
                    sender.lock().unwrap().send(content.clone())?;
                }
            }
            Err(e) => eprintln!("Error: {}", e),
        }
    }

    Ok(())
}
