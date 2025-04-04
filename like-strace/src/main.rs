extern crate pretty_env_logger;
#[macro_use]
extern crate log;
use clap::Parser;
use libc::syscall;
use log::info;
use nix::sys::ptrace;
use nix::sys::wait::waitpid;
use nix::unistd::Pid;
use std::{
    os::unix::process::CommandExt,
    process::Command,
    time::{Duration, Instant},
};
#[cfg(target_arch = "aarch64")]
use syscalls::aarch64::Sysno;
#[cfg(target_arch = "x86")]
use syscalls::x86_64::Sysno;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    #[arg(short, long)]
    c: bool,
    command: String,
    args: Vec<String>,
}

#[derive(Default)]
struct CallStats {
    count: u32,
    total_time: Duration,
    errors: u32,
}

fn main() {
    pretty_env_logger::init();
    info!("like-strace start");
    let args = Args::parse();

    #[cfg(target_arch = "x86")]
    {
        // 步骤2：创建子进程
        trace!("spawn child process...");
        let child = unsafe {
            Command::new(&args.command)
                .args(&args.args)
                .pre_exec(|| {
                    // 步骤3：附加ptrace
                    ptrace::traceme()
                        .map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))?;
                    Ok(())
                })
                .spawn()
                .expect("Failed to spawn child process")
        };

        let pid = Pid::from_raw(child.id() as i32);
        let mut stats = std::collections::HashMap::new();
        let start_time = Instant::now();
        // 等待子进程进入ptrace停止状态
        trace!("wait for child process to stop...");
        waitpid(pid, None).expect("ptrace error");
        ptrace::syscall(pid, None).expect("continue error");

        loop {
            // 步骤3：等待系统调用事件
            trace!("------------");
            trace!("wait for syscall event...");
            match waitpid(pid, None) {
                Ok(status) => {
                    if let nix::sys::wait::WaitStatus::Exited(_pid, _exit_status) = status {
                        break;
                    }
                }
                Err(e) => {
                    trace!("waitpid error: {:?}", e);
                    break;
                }
            }

            // 步骤4：获取寄存器状态
            let regs = ptrace::getregs(pid).unwrap();
            let syscall = Sysno::from(regs.orig_rax as u32);
            let entry_time = Instant::now();

            // 记录系统调用信息
            if !args.c {
                trace!("syscall: {:?}, rax: {:?}", syscall, regs.orig_rax);
                println!("{:?} ({})", syscall, regs.orig_rax);
            }

            // 继续执行
            trace!("continue syscall event...");
            ptrace::syscall(pid, None).unwrap();

            // 等待系统调用退出
            trace!("wait for syscall exit...");
            waitpid(pid, None).unwrap();

            // 计算耗时
            let elapsed = entry_time.elapsed();
            let entry = stats.entry(syscall).or_insert(CallStats::default());
            entry.count += 1;
            entry.total_time += elapsed;
            if let Ok(regs) = ptrace::getregs(pid) {
                if (regs.rax as i64) < 0 {
                    entry.errors += 1;
                }
            }
            // Handle exit_group syscall to avoid panic
            // if syscall == Sysno::exit_group {
            //     break;
            // }
            if let Err(e) = ptrace::syscall(pid, None) {
                if e == nix::errno::Errno::ESRCH && syscall == Sysno::exit_group {
                    break;
                }
                trace!("ptrace::syscall error: {:?}", e);
                break;
            }
        } // 步骤5：输出统计信息
        if args.c {
            let total_time = start_time.elapsed();
            println!("% time     seconds  usecs/call     calls    errors syscall");
            println!("------ ----------- ----------- --------- --------- ----------------");

            let mut entries: Vec<_> = stats.iter().collect();
            entries.sort_by(|a, b| b.1.total_time.cmp(&a.1.total_time));

            for (syscall, data) in entries {
                let percent = (data.total_time.as_secs_f64() / total_time.as_secs_f64()) * 100.0;
                let avg = data.total_time.as_micros() as f64 / data.count as f64;
                println!(
                    "{:6.2} {:11.6} {:11.0} {:9} {:9} {:?}",
                    percent,
                    data.total_time.as_secs_f64(),
                    avg,
                    data.count,
                    data.errors,
                    syscall
                );
            }
        }
    }
}
