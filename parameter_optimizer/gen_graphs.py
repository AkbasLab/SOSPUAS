#!/bin/python

import argparse
import os
import sys
import json
import numpy as np
import matplotlib.pyplot as plt
import bisect
from pathlib import Path
from sklearn.linear_model import LinearRegression

import matplotlib as mpl
# Use true LaTeX and bigger font
#mpl.rc('text', usetex=True)
# Include packages `amssymb` and `amsmath` in LaTeX preamble
# as they include extended math support (symbols, envisonments etc.)
#mpl.rcParams['text.latex.preamble'] = [r"\usepackage{amssymb}",
#                                       r"\usepackage{amsmath}"]

parser = argparse.ArgumentParser(description='Exports json simulation data to graphs')
parser.add_argument('file', default=None,
                    help='The file or directory to export. If directory, all matching json files will be exported')
parser.add_argument('--prefix', default="",
                    help='A prefix which is written before each exported graph')
parser.add_argument('--one-graph', action='store_true',
                    help='Export a single graph with all the data points')

args = parser.parse_args()
if not os.path.exists(args.file):
    print("ERROR: File {} does not exist!".format(args.file))
    sys.exit(1)

fig = None
ax = None

series_format_order = ""
d_values_map = {}

x_param_expected = "a"
y_param_expected = "r"

def export_single(path, prefix):
    '''Exports the data from a single json file to a graph
    '''

    global fig
    global ax
    global series_format_order
    global d_values_map
    # Called multiple times with a different figure each time
    if not args.one_graph:
        fig = plt.figure()
    else:
        if fig == None:
            fig = plt.figure()
            print("Set figure")

    obj = json.load(open(path))
    params = obj["params"]

    x_param = params[0]["name"]
    y_param = params[1]["name"]
    if x_param.strip() != x_param_expected:
        print("x parameter is not a: " + x_param)
        sys.exit(1)

    if y_param.strip() != y_param_expected:
        print("y parameter is not r: " + y_param)
        sys.exit(1)

    #print("X-axis param is " + x_param)
    #print("Y-axis param is " + y_param)

    results = obj["results"]
    errors = []
    run_params = []
    x_values = []
    y_values = []
    for result in results:
        params = result["parameters"]
        errors.append(result["fitness"])
        run_params.append(params)
        x_values.append(params[x_param])
        y_values.append(params[y_param])

    std_range = 1.5
    x_mean = np.mean(x_values)
    x_stddev = np.std(x_values)
    y_mean = np.mean(y_values)
    y_stddev = np.std(y_values)

    x_min = max(x_mean - x_stddev * std_range, 0.0)
    x_max = x_mean + x_stddev * std_range
    y_min = max(y_mean - y_stddev * std_range, 0.0)
    y_max = y_mean + y_stddev * std_range

    # Only display values within `std_range` standard deviations of the mean
    i = 0
    while True:
        if i >= len(x_values):
            break
        remove = False
        if x_values[i] < x_min or x_values[i] > x_max:
            remove = True
        if y_values[i] < y_min or y_values[i] > y_max:
            remove = True
        if remove:
            x_values.pop(i)
            y_values.pop(i)
            errors.pop(i)
        else:
            i += 1

    # 256 numbers which will be used to query value for that percentile
    hundred_numbers = np.arange(0.0, 100, step=100/256)
    percentiles = np.percentile(errors, hundred_numbers)
    colors = []
    for error in errors:
        index = bisect.bisect_left(percentiles, error)
        colors.append((index / 256, (256 - index) / 256, 0.3))

    #Remove bad data points if we are rendering to a single graph
    if args.one_graph:
        colors = None
        i = 0
        while True:
            if i >= len(x_values):
                break
            # How good is this data point?
            index = bisect.bisect_left(percentiles, errors[i])
            # Remove if not in the to 20%
            if index > 20 * (256 / 100):
                x_values.pop(i)
                y_values.pop(i)
                errors.pop(i)
            else:
                i += 1

    if not args.one_graph:
        ax = fig.add_subplot(111)
    else:
        if ax == None:
            print("Made subplot")
            ax = fig.add_subplot(111)
    #ax = fig.axes()
    ax.scatter(x_values, y_values, s=2, c=colors)

    # Do linear regression and display line
    weights = []
    for error in errors:
        if error > percentiles[int(10 * 256 / 100)]:
            # Give 0 weight to data points below the 90th percentile
            # Indexing looks odd because `len(percentiles == 256`
            weights.append(0)
        else:
            weights.append(1000.0 / error / error)

    feed_x = np.array(x_values).reshape(-1, 1)
    feed_y = np.array(y_values)

    model = LinearRegression()
    model.fit(feed_x, feed_y, sample_weight=weights)

    x_new = np.linspace(x_min, x_max, 200)
    y_new = model.predict(x_new[:, np.newaxis])

    parent_path = Path(path).parent
    label_path = os.path.join(parent_path, "label.txt")
    if os.path.exists(label_path):
        f = open(label_path) 
        label = f.read()
        if len(label) > 30:
            print("WARN: Label file " + label_path + " is over 30 characters. Data may be obstructed on graph")
        if len(label) == 0:
            print("WARN: Label file " + label_path + " empty")
        else:
            parts = list(map(lambda x: x.split("="), label.strip().split(" ")))
            vars = {}
            for tup in list(parts):
                vars[tup[0]] = float(tup[1])

            d = vars['d']
            #Write data points
            if d in d_values_map:
                obj = d_values_map[d]
            else:
                obj = {
                'x_values': [],
                'y_values': [],
            }
            obj['x_values'].extend(x_values)
            obj['y_values'].extend(y_values)

            d_values_map[d] = obj

    else:
        print("Missing label " + label_path)
        print("Edit file with desired label and re-run to fix")
        label=path

    ax.plot(x_new, y_new, label=label.strip())

    print("y=", model.coef_, "x + ", model.intercept_, " for ", label)
    ax.set_xlabel(x_param)
    ax.set_ylabel(y_param)
    if not args.one_graph:

        ax.axis('tight')
        name = prefix + "hot_cold.png"
        fig.savefig(name)
        print("Wrote " + name)


def run_distance_analysis():
    '''Runs analysis for different values of D using the a and r data points collected from calling `export_single`
    '''
    global d_values_map
    print(str(d_values_map))
    d_values = []
    da_dr_values = []
    for d, values in d_values_map.items():

        feed_x = np.array(values["x_values"]).reshape(-1, 1)
        feed_y = np.array(values["y_values"])

        model = LinearRegression()
        model.fit(feed_x, feed_y)
        r_2 = model.score(feed_x, feed_y)
        print("for d ", d, "r=", model.coef_, "a + ", model.intercept_, " (r^2", r_2, ")")

        d_values.append(d)
        da_dr_values.append(model.coef_[0])
 
        fig = plt.figure()
        ax = fig.add_subplot(111)
        ax.scatter(feed_x, feed_y)
        ax.set_xlabel("a")
        ax.set_ylabel("r")

        x_new = np.linspace(np.min(feed_x), np.max(feed_y), 200)
        y_new = model.predict(x_new[:, np.newaxis])
        ax.plot(x_new, y_new)

        fig.savefig("all" + str(d) + ".png")
        plt.clf()

    
    fig = plt.figure()
    ax = fig.add_subplot(111)
    feed_x = np.array(d_values).reshape(-1, 1)
    feed_y = np.array(da_dr_values)

    ax.scatter(feed_x, feed_y)
    ax.set_xlabel("d")
    ax.set_ylabel("da/dr")
    print("X: " + str(d_values))
    print("Y: " + str(da_dr_values))

    model = LinearRegression()

    model.fit(feed_x, feed_y)
    r_2 = model.score(feed_x, feed_y)
    print("FINAL REGRESSION ", "da_dr=", model.coef_, "d + ", model.intercept_, " (r^2", r_2, ")")

    x_new = np.linspace(np.min(feed_x), np.max(feed_x), 200)
    y_new = model.predict(x_new[:, np.newaxis])
    ax.plot(x_new, y_new)

    fig.savefig("overall.png")
    plt.clf()

multiple = os.path.isdir(args.file)

if multiple:
    to_export = []
    for dirpath, dirnames, files in os.walk(args.file):
        for name in files:
            parent_dir = os.path.basename(os.path.abspath(dirpath))
            if name.lower().endswith(".json"):
                to_export.append((dirpath, name, parent_dir))

    #Sort by the label values
    #to_export.sort(key=lambda x: open(os.path.join(x[0], "label.txt")).read())
    #Sort by the d value in 'd=XXX tx=...'
    to_export.sort(key=lambda x: float(open(os.path.join(x[0], "label.txt")).read().split("=")[1].split(" ")[0]))
    for i in range(0, len(to_export)):
        dirpath = to_export[i][0]
        name = to_export[i][1]
        parent_dir = to_export[i][2]

        export_single(os.path.join(dirpath, name), args.prefix + parent_dir)
else:
    export_single(args.file, args.prefix)

if args.one_graph:
    print("Saved figure")
    fig.legend()
    fig.savefig(args.prefix + "hot_cold.png") 
    run_distance_analysis()

