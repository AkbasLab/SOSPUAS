use crate::position_parser::Vec3;
use std::process::Command;

pub fn run_waf_command(
    path: &str,
    command: &str,
    env: std::collections::HashMap<&str, &str>,
) -> Result<(), crate::Error> {
    let mut waf_path = std::fs::canonicalize(path).unwrap().to_owned();
    waf_path.push("waf");
    let arg = format!("{} {}", waf_path.to_str().unwrap(), command);
    println!("Running: {}", arg);

    if Command::new("bash")
        .current_dir(path)
        .arg("-c")
        .arg(arg)
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

/// Macro that creates a map from key value pairs in the following format:
/// ("Key1" => 1, "Key2" => 2)
#[macro_export]
macro_rules! map {
    // map-like
    ($($k:expr => $v:expr),* $(,)?) => {
        std::iter::Iterator::collect(std::array::IntoIter::new([$(($k, $v),)*]))
    };
}

fn lerp<T, F>(a: T, b: T, f: F) -> T
where
    T: Copy,
    T: std::ops::Sub<Output = T>,
    T: std::ops::Add<Output = T>,
    T: std::ops::Mul<F, Output = T>,
{
    //Convert the 0-1 range into a value in the right range.
    return a + ((b - a) * f);
}

fn normalize<T, F>(a: T, b: T, value: T) -> F
where
    T: Copy,
    T: std::ops::Sub<Output = T>,
    T: std::ops::Div<Output = F>,
{
    return (value - a) / (b - a);
}

pub fn map<S, D, F>(left_min: S, left_max: S, value: S, right_min: D, right_max: D) -> D
where
    S: Copy,
    S: std::ops::Sub<Output = S>,
    S: std::ops::Div<Output = F>,
    D: Copy,
    D: std::ops::Sub<Output = D>,
    D: std::ops::Add<Output = D>,
    D: std::ops::Mul<F, Output = D>,
{
    //Figure out how 'wide' each range is
    let f: F = normalize(left_min, left_max, value);

    lerp(right_min, right_max, f)
}
