use crate::position_parser::{SimulationData, TimePoint};

use glam::Vec3A;
use once_cell::sync::OnceCell;
use plotters::prelude::*;
use rand::{distributions::Alphanumeric, Rng};

use std::collections::HashMap;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Instant;

struct Parameter {
    name: String,
    optim: tpe::TpeOptimizer,
}

type State = Arc<Mutex<StateImpl>>;

struct SimulationRun {
    /// The parameters used in this run
    parameters: HashMap<String, f64>,
    /// The fitness score of this run
    fitness: f64,
    /// The time this run finished
    time: Instant,
}

struct StateImpl {
    /// The parameters in use. The `name` value in the Parameter struct corresponds with the
    /// key in a `SimulationRun`'s `parameters` map
    params: Vec<Parameter>,

    /// Finished runs
    results: Vec<SimulationRun>,
}

static RUNNING: AtomicBool = AtomicBool::new(true);
static PATH: OnceCell<String> = OnceCell::new();
static STATE: OnceCell<State> = OnceCell::new();
static BASE_ARGUMENTS: [&str; 1] = ["--duration=180"];
static BEST_FITNESS: atomic_float::AtomicF64 = atomic_float::AtomicF64::new(10000.0);

pub fn run(path: &str) {
    ctrlc::set_handler(|| {
        RUNNING.store(false, Ordering::Relaxed);
        println!(" Shutting down runners");
    })
    .expect("failed to to set Control-C handler");

    let param_max = 10.0;
    let _ = STATE.set(Arc::new(Mutex::new(StateImpl {
        params: vec![
            Parameter {
                name: "a".to_owned(),
                optim: tpe::TpeOptimizer::new(
                    tpe::parzen_estimator(),
                    tpe::range(0.0, param_max).unwrap(),
                ),
            },
            Parameter {
                name: "r".to_owned(),
                optim: tpe::TpeOptimizer::new(
                    tpe::parzen_estimator(),
                    tpe::range(0.0, param_max).unwrap(),
                ),
            },
        ],
        results: Vec::new(),
    })));
    let default_fitness = BEST_FITNESS.load(Ordering::Relaxed);
    for param in STATE.get().unwrap().lock().unwrap().params.iter_mut() {
        // Fill in default values so parameters start around 1 by default
        param.optim.tell(1.0, default_fitness).unwrap();
    }

    let mut threads = Vec::new();
    let _ = PATH.set(path.to_owned());
    for _ in 0..num_cpus::get() {
        threads.push(std::thread::spawn(run_thread));
    }
    println!("Runners started");
    for thread in threads {
        let _ = thread.join();
    }

    println!("All runners stopped");
    let state = STATE.get().unwrap().lock().unwrap();
    println!("Exporting results from {} simulations", state.results.len());

    write_hot_cold(&state, "hot_cold.png").unwrap();
    write_fitness_time(&state, "fitness_time.png").unwrap();
}

fn write_hot_cold(state: &StateImpl, file_name: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut fitness_scores: Vec<f64> = state
        .results
        .iter()
        .map(|r| r.fitness)
        .filter(|v| !v.is_nan())
        .collect();

    fitness_scores.sort_by(|a, b| a.partial_cmp(b).unwrap());

    //We want to color the points on the scatteragram based on the fitness for that point.
    //Finding the best fitness and assigning it green, and the worst fitness red, and then
    //interpolating the rest results is most of the points being green because the fitness scores
    //are not distributed evenly across the (best_fitness..worst_fitness) range.
    //To combat this we will make a frequency table so that fitness scores in the top 10-20% range
    //will be 80-90% green and the rest red. This will be repeated for all points with the step
    //size being 1/256 * 100 % to make it very smooth

    let step_size = 256;
    let smoother = crate::util::RangeSmoother::new(step_size, fitness_scores.as_slice());
    let smoothed_values: Vec<_> = smoother.ranges().collect();

    let params_to_draw: Vec<&String> = state.results[0].parameters.keys().take(2).collect();
    let points: Vec<_> = state
        .results
        .iter()
        .map(|result| {
            let params_used = &result.parameters;
            let x = params_used[params_to_draw[0]];
            let y = params_used[params_to_draw[1]];
            (x, y, result.fitness)
        })
        .collect();

    let root = BitMapBackend::new(file_name, (1024, 768)).into_drawing_area();

    root.fill(&WHITE)?;
    let x_param = params_to_draw[0];
    let y_param = params_to_draw[1];
    root.titled(
        format!("{} vs. {}", x_param, y_param).as_str(),
        ("sans-serif", 40),
    )?;

    let areas = root.split_by_breakpoints([944], [80]);

    let mut scatter_ctx = ChartBuilder::on(&areas[2])
        .x_label_area_size(40)
        .y_label_area_size(40)
        .build_cartesian_2d(0f64..1.2f64, 0f64..2.5f64)?;

    scatter_ctx
        .configure_mesh()
        .disable_x_mesh()
        .disable_y_mesh()
        .x_desc(x_param)
        .y_desc(y_param)
        .draw()?;

    scatter_ctx.draw_series(points.iter().map(|(x, y, fitness)| {
        let mut i = 0;
        for limit in smoothed_values.iter() {
            i += 1;
            if fitness < limit {
                break;
            }
        }

        let color = plotters::style::RGBColor(i as u8, (256 - i) as u8, 50);
        Circle::new((*x, *y), 2, color.filled())
    }))?;

    root.present().expect("Unable to write image to file");

    Ok(())
}

fn write_fitness_time(
    state: &StateImpl,
    file_name: &str,
) -> Result<(), Box<dyn std::error::Error>> {
    let root = BitMapBackend::new(file_name, (1024, 768)).into_drawing_area();
    root.fill(&WHITE)?;
    if state.results.is_empty() {
        println!("No data to graph");
        return Ok(());
    }
    let worst_fitness = state
        .results
        .iter()
        .map(|e| e.fitness)
        .max_by(|a, b| a.partial_cmp(b).unwrap())
        .unwrap() as f32 * 0.7;//Scale down to hide outliers

    let start = state.results.first().unwrap().time;
    let end = state.results.last().unwrap().time;

    let mut chart = ChartBuilder::on(&root)
        .x_label_area_size(40)
        .y_label_area_size(40)
        .caption("Fitness Score vs. Time", ("sans-serif", 50.0).into_font())
        .build_cartesian_2d(0.0..(end - start).as_secs_f32(), 0f32..worst_fitness)?;

    chart.configure_mesh().light_line_style(&WHITE).draw()?;

    chart.draw_series(state.results.iter().map(|r| {
        Circle::new(
            ((r.time - start).as_secs_f32(), r.fitness as f32),
            2,
            &BLACK,
        )
    }))?;

    chart
        .draw_series(LineSeries::new(
            state.results.chunks(num_cpus::get() * 3 / 2).map(|runs| {
                let x = runs
                    .iter()
                    .map(|r| (r.time - start).as_secs_f32())
                    .sum::<f32>()
                    / runs.len() as f32;
                let y = runs.iter().map(|r| r.fitness as f32).sum::<f32>() / runs.len() as f32;

                (x, y)
            }),
            &BLUE,
        ))?
        .label("Average fitness");

    Ok(())
}

fn run_binary(
    rel_working_dir: &str,
    rel_bin_path: &str,
    args: &[String],
) -> Result<(), Box<dyn std::error::Error>> {
    let mut base = std::env::current_dir().unwrap();
    base.push(rel_working_dir);
    //We need the NS3 libs to be in LD_LIBRARY_PATH
    let lib_path = {
        let mut base = base.clone();
        base.push("build");
        base.push("lib");
        base
    };
    let current_dir = base.clone();
    base.push(rel_bin_path);
    let bin_path = base;

    if Command::new(bin_path)
        .current_dir(current_dir)
        .env("LD_LIBRARY_PATH", lib_path.to_str().unwrap())
        .args(args)
        .spawn()?
        .wait()?
        .success()
    {
        Ok(())
    } else {
        Err("Error running binary".into())
    }
}

fn run_thread() {
    let mut rng = rand::thread_rng();
    let mut param_map = HashMap::new();
    let mut args: Vec<String> = Vec::new();
    for arg in BASE_ARGUMENTS.iter() {
        args.push((*arg).to_owned());
    }

    while RUNNING.load(Ordering::Relaxed) {
        let pos_file_name: String = rand::thread_rng()
            .sample_iter(&Alphanumeric)
            .take(10)
            .map(char::from)
            .collect();

        //Keep base arguments
        args.resize(BASE_ARGUMENTS.len(), String::new());

        let ns3_path = PATH.get().unwrap();
        let mut buf = PathBuf::from(ns3_path);
        buf.push(pos_file_name);
        buf.set_extension("csv");
        let mut positions_file = std::env::current_dir().unwrap();
        positions_file.push(buf);
        args.push(format!(
            "--positionsFile={}",
            &positions_file.to_str().unwrap()
        ));

        {
            let mut state = STATE.get().unwrap().lock().unwrap();
            param_map.clear();
            for param in state.params.iter_mut() {
                let value = param.optim.ask(&mut rng).unwrap();
                param_map.insert(param.name.clone(), value);
                args.push(format!("--{}={}", param.name, value));
            }
        };

        //Run simulation
        match run_binary(ns3_path, "build/scratch/non-ideal/non-ideal", &args) {
            Ok(_) => match run_analysis(&positions_file, &param_map, &positions_file) {
                Ok(_) => {}
                Err(err) => {
                    println!("Error while doing analysis: {}", err);
                }
            },
            Err(err) => {
                println!("Error while running waf: {}", err);
                let _ = std::fs::remove_file(positions_file);
            }
        }
    }
    println!("Runner exiting cleanly");
}

fn get_fitness(data: &mut SimulationData) -> f64 {
    let time_step = 0.1;
    let mut time = 0.0;
    let mut last_poses = HashMap::new();
    let uavs = data.uavs.clone();
    let central_node = uavs.iter().min().unwrap();

    let mut all_distances = Vec::new();
    let mut all_velocities = Vec::new();
    let mut under_mad_threshold_time = None;
    while time <= data.simulation_length {
        let mut distances: Vec<f64> = Vec::new();
        let mut velocities: Vec<f64> = Vec::new();

        let central_pos = data.pos_at_time(TimePoint(time), *central_node).unwrap();
        for uav in &uavs {
            if let Some(now_pos) = data.pos_at_time(TimePoint(time), *uav) {
                match last_poses.get(uav) {
                    None => {}
                    Some((last_pos, last_time)) => {
                        let pos_delta = now_pos - *last_pos;
                        let time_delta = time - last_time;
                        let velocity: Vec3A = pos_delta / time_delta;
                        velocities.push(velocity.length() as f64);
                    }
                }
                last_poses.insert(uav, (now_pos, time));
                if uav != central_node {
                    distances.push((now_pos - central_pos).length() as f64);
                }
            }
        }
        let distances_mean = rgsl::statistics::mean(&distances, 1, distances.len());
        let mad_of_distance = rgsl::statistics::absdev(&distances, 1, distances.len());
        let mean_velocity = rgsl::statistics::mean(&velocities, 1, velocities.len());
        let mad_percent = mad_of_distance * distances_mean * 100.0;
        let mad_threshold = 180.0;
        match under_mad_threshold_time {
            Some(_) => {
                if mad_percent >= mad_threshold {
                    //Too high to hold streak
                    under_mad_threshold_time = None;
                }
            }
            None => {
                if mad_percent < mad_threshold {
                    //Start streak
                    under_mad_threshold_time = Some(time);
                }
            }
        }
        all_distances.push((time, distances_mean));
        all_velocities.push((time, mean_velocity));
        //println!("T: {}, V: {}, D: {}", time, mean_velocity, mad_of_distance);

        time += time_step;
    }
    let stable_time = under_mad_threshold_time.unwrap_or(180.0) as f64;
    let mean_velocity: f64 =
        all_velocities.iter().map(|(_, v)| *v).sum::<f64>() / all_velocities.len() as f64;

    let average_distance =
        all_distances.iter().map(|(_, v)| *v).sum::<f64>() / all_distances.len() as f64;

    let desired_distance_cost = 200.0 * (3.0 - average_distance).abs();
    let stable_time_cost = 1.0 * stable_time;
    let velocity_cost = 250.0 * mean_velocity;
    println!(
        "Final costs: distance: {}, stable time: {}, vel: {}",
        desired_distance_cost, stable_time_cost, velocity_cost
    );

    desired_distance_cost + stable_time_cost + velocity_cost
}

fn run_analysis(
    pos_path: &std::path::Path,
    param_map: &HashMap<String, f64>,
    positions_file: &std::path::Path,
) -> Result<(), Box<dyn std::error::Error>> {
    //let start = Instant::now();
    let positions = String::from_utf8(std::fs::read(&pos_path)?)?;
    let mut data = SimulationData::parse(&positions)?;
    let fitness = get_fitness(&mut data);
    println!("FITNESS: {}", fitness);
    {
        let mut state = STATE.get().unwrap().lock().unwrap();
        for param in state.params.iter_mut() {
            let value = param_map.get(&param.name).unwrap();
            param.optim.tell(*value, fitness).unwrap();
        }
        state.results.push(SimulationRun {
            parameters: param_map.clone(),
            time: Instant::now(),
            fitness,
        });
    }
    let old_fitness = BEST_FITNESS.load(Ordering::Relaxed);
    if fitness < old_fitness {
        //If multiple threads get in here we don't really care...
        BEST_FITNESS.store(fitness, Ordering::Relaxed);
        let src = positions_file;
        let mut dest = PathBuf::from(positions_file);
        dest.pop(); //Pop positions csv file name
        dest.push("out");
        dest.push(format!("{}.csv", fitness));
        std::fs::copy(src, dest).unwrap();
        println!("Got best fitness: {} for params: {:?}", fitness, param_map);
    }

    if let Some(err) = std::fs::remove_file(pos_path).err() {
        println!(
            "failed to delete temp positions file: {} - {}",
            pos_path.to_str().unwrap(),
            err
        );
    }
    Ok(())
}
