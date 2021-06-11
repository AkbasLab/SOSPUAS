use std::io::Read;
use std::{fmt, process::Command};

struct RepoInfo {
    url: String,
    path: String,
    commit_hash: String,
}

type Error = Box<dyn std::error::Error>;

#[derive(Debug)]
struct GitError {
    command: String,
    message: String,
    exit_code: std::process::ExitStatus,
}

impl GitError {
    fn new(
        command: String,
        message: impl Into<String>,
        exit_code: std::process::ExitStatus,
    ) -> Self {
        GitError {
            command,
            message: message.into(),
            exit_code,
        }
    }
}

impl std::error::Error for GitError {}

impl fmt::Display for GitError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "Error running: {}, Exit Code: {}: {}",
            self.command, self.exit_code, self.message
        )
    }
}

macro_rules! collection {
    // map-like
    ($($k:expr => $v:expr),* $(,)?) => {
        std::iter::Iterator::collect(std::array::IntoIter::new([$(($k, $v),)*]))
    };
    // set-like
    ($($v:expr),* $(,)?) => {
        std::iter::Iterator::collect(std::array::IntoIter::new([$($v,)*]))
    };
}

fn run_git_command(args: &[&str], current_dir: &str) -> Result<String, Error> {
    let mut process = Command::new("git")
        .current_dir(current_dir)
        .args(args)
        .spawn()?;

    let exit_code = process.wait()?;
    let mut buf = String::new();
    match process.stdout {
        Some(mut stdout) => {
            let _ = stdout.read_to_string(&mut buf)?;
        }
        None => {}
    }
    if !exit_code.success() {
        let message = args.join(" ").to_owned();
        Err(GitError::new(message, buf, exit_code).into())
    } else {
        Ok(buf)
    }
}

fn main() {
    let url = "https://github.com/TroyNeubauer/NS3NonIdealConditions2021.git";
    let path = "./NS3";
    let needs_configure = match setup_repo(&RepoInfo {
        url: url.to_owned(),
        path: path.to_owned(),
        commit_hash: "92efaf818d46fb18a45ff3b5bbf1f53dafc2b9d4".to_owned(),
    }) {
        Ok(needs_configure) => needs_configure,
        Err(err) => {
            eprintln!("Error while setting up repo: {}", err);
            return;
        }
    };
    if needs_configure {
        println!("Running configure");
        run_waf_command(
            path,
            "configure --build-profile=optimized",
            collection!("CXXFLAGS" => "-Wall"),
        )
        .unwrap();
    }

    run_waf_command(path, "--run non-ideal", collection!("" => "")).unwrap();
}

fn setup_repo(info: &RepoInfo) -> Result<bool, Error> {
    let mut needs_configure = false;
    if !std::path::Path::new(&info.path).exists() {
        println!("Cloning repo: {}", info.url);
        run_git_command(&["clone", info.url.as_str(), info.path.as_str()], "./")?;
        needs_configure = true;
    }
    let current_hash = run_git_command(&["rev-parse", "HEAD"], info.path.as_str())?;

    println!("Checkout complete!");
    if current_hash == info.commit_hash {
        let _ = run_git_command(&["checkout", info.commit_hash.as_str()], info.path.as_str())?;
        //We just checked out a new commit so reconfigure!
        Ok(true)
    } else {
        Ok(needs_configure)
    }
}

fn run_waf_command(
    path: &str,
    command: &str,
    env: std::collections::HashMap<&str, &str>,
) -> Result<(), Error> {
    let mut waf_path = std::fs::canonicalize(path).unwrap().to_owned();
    waf_path.push("waf");
    println!("Running waf at {}", waf_path.to_str().unwrap());

    if Command::new("bash")
        .current_dir(path)
        .arg("-c")
        .arg(format!("{} {}", waf_path.to_str().unwrap(), command))
        .envs(env)
        .spawn()?
        .wait()?
        .success()
    {
        Ok(())
    } else {
        Err(format!("Failed to run command: waf {}", command).into())
    }
}
