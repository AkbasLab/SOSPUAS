use std::collections::HashMap;

mod git;
mod optimization;
mod position_parser;
mod util;

type Error = Box<dyn std::error::Error>;

fn main() {
    let url = "https://github.com/TroyNeubauer/NS3NonIdealConditions2021.git";
    let path = "NS3".to_owned();
    let needs_configure = match git::setup_repo(&git::RepoInfo {
        url: url.to_owned(),
        path: path.to_owned(),
        commit_hash: "fd23b226569d82ac6a55ffec5c6899a647a2b309".to_owned(),
    }) {
        Ok(needs_configure) => needs_configure,
        Err(err) => {
            eprintln!("Error while setting up repo: {}", err);
            return;
        }
    };
    if needs_configure {
        println!("Running configure");
        util::run_waf_command(
            &path,
            "configure --build-profile=optimized",
            map!("CXXFLAGS" => "-Wall"),
        )
        .unwrap();
    }

    util::run_waf_command(&path, "build", HashMap::new()).expect("failed to build waf");

    optimization::run(&path);
}
