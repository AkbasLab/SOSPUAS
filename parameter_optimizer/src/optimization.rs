use crate::util;
use rand::{distributions::Alphanumeric, Rng};

use once_cell::sync::OnceCell;
use std::sync::atomic::{AtomicBool, Ordering};

static RUNNING: AtomicBool = AtomicBool::new(true);
static PATH: OnceCell<String> = OnceCell::new();

pub fn run(path: &String) {
    let mut threads = Vec::new();
    let _ = PATH.set(path.clone());
    for _ in 0../*num_cpus::get()*/1 {
        threads.push(std::thread::spawn(run_thread));
    }
    for thread in threads {
        let _ = thread.join();
    }
}

fn run_thread() {
    while RUNNING.load(Ordering::Relaxed) {
        let mut positions_file: String = rand::thread_rng()
            .sample_iter(&Alphanumeric)
            .take(10)
            .map(char::from)
            .collect();

        positions_file.push_str(".csv");
        util::run_waf_command(
            PATH.get().unwrap(),
            format!("--run \"non-ideal --positionsFile={}\"", positions_file).as_str(),
            crate::map!("NS_LOG" => "UAV:UAV_MAIN"),
        )
        .unwrap();
    }
}
