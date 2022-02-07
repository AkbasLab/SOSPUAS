#!/bin/python

import argparse
import os
import sys
import json
import numpy as np
import matplotlib.pyplot as plt
import bisect
from sklearn.linear_model import LinearRegression

parser = argparse.ArgumentParser(description='Exports json simulation data to graphs')
parser.add_argument('file', default=None,
                    help='The file or directory to export. If directory, all matching json files will be exported')
parser.add_argument('--prefix', default="",
                    help='A prefix which is written before each exported graph')

args = parser.parse_args()
if not os.path.exists(args.file):
    print("ERROR: File {} does not exist!".format(args.file))
    sys.exit(1)


def export_single(path, prefix):
    '''Exports the data from a single json file to a graph
    '''
    obj = json.load(open(path))
    params = obj["params"]
    x_param = params[1]["name"]
    y_param = params[0]["name"]
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

    ax = plt.axes()
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

    ax.plot(x_new, y_new)

    ax.set_xlabel(x_param)
    ax.set_ylabel(y_param)

    ax.axis('tight')

    name = prefix + "hot_cold.png"
    plt.savefig(name)
    print("Wrote " + name)
    print("  y=", model.coef_, "x + ", model.intercept_)
    print()
    plt.clf()
    

multiple = os.path.isdir(args.file)

if multiple:
    for dirpath, dirnames, files in os.walk(args.file):
        for name in files:
            if name.lower().endswith(".json"):
                export_single(os.path.join(dirpath, name), args.prefix + os.path.basename(os.path.abspath(dirpath)))

else:
    export_single(args.file, args.prefix)


