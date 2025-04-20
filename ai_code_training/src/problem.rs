use serde::{Serialize, Deserialize};

/// 最终输出的标准化问题结构
#[derive(Debug, Serialize, Deserialize)]
pub struct LeetCodeProblem {
    pub title: String,
    pub description: String,
    pub difficulty: String,
    pub tags: Vec<String>,
    pub code_snippets: std::collections::HashMap<String, String>,
}

pub fn print_leet_code_problem(problem: &LeetCodeProblem) {
    println!("title: {}", problem.title);
    println!("description: {}", problem.description);
    println!("difficulty: {}", problem.difficulty);
    println!("tags: {:?}", problem.tags);
    println!("code snippets: {}", problem.code_snippets.get("c").unwrap());
}