#!/usr/bin/env python3
"""Compare the saved T0/Omega0 force-element and OpenFOAM load histories."""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

import numpy as np


DT = 0.005
TIME_ATOL = 1e-8  # seconds, well below one step and above the text output roundoff
RHO = 1.0  # kg/m³, both FEM objects and forces/rhoInf in Omega0
FEM_FIELDS = (
    "Time",
    "SumFlowStructureContribution",
    "SumNegativeFlowStructure",
    "SumPositiveFlowStructure",
    "SumWallVorticityContribution",
    "SumWallShearStressContribution",
    "SumWallVelocityContribution",
)
INPUTS = {
    "drag": "postProcessing/FEMDrag/0/force_FEM.dat",
    "lift": "postProcessing/FEMLift/0/force_FEM.dat",
    "forces": "postProcessing/forces/0/force.dat",
    "coefficients": "postProcessing/forceCoeffs/0/coefficient.dat",
}


def read_fem(path):
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if not set((*FEM_FIELDS, "wall")).issubset(reader.fieldnames or []):
            raise ValueError(f"{path}: unexpected FEM columns")
        rows = []
        for row in reader:
            if row["wall"] != "wall":
                raise ValueError(f"{path}:{reader.line_num}: expected the single patch 'wall'")
            rows.append([float(row[field]) for field in FEM_FIELDS])
    return np.asarray(rows)


def read_openfoam_table(path, fields):
    metadata = {}
    columns = None
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            header = line.lstrip("#").strip()
            if line.startswith("#") and ":" in header:
                name, value = header.split(":", 1)
                metadata[name.strip()] = value.strip()
            if line.startswith("#") and header.split()[:1] == ["Time"]:
                columns = header.split()
                break
        if columns is None or not set(fields).issubset(columns):
            raise ValueError(f"{path}: expected columns {', '.join(fields)}")
        rows = np.loadtxt(stream, usecols=[columns.index(field) for field in fields], ndmin=2)
    return rows, metadata


def select_steps(path, rows, expected):
    if rows.ndim != 2 or not len(rows) or not np.isfinite(rows).all():
        raise ValueError(f"{path}: empty or non-finite history")
    times = rows[:, 0]
    steps = np.rint(times / DT).astype(np.int64)
    inside = (steps >= expected[0]) & (steps <= expected[-1])
    if not np.array_equal(steps[inside], expected):
        raise ValueError(f"{path}: missing, duplicate or unordered steps in the comparison window")
    if np.any(np.abs(times[inside] - expected * DT) > TIME_ATOL):
        raise ValueError(f"{path}: timestamps do not match the {DT:g} s output grid")
    return rows[inside, 1:]


def max_abs(values):
    return float(np.max(np.abs(values)))


def rms(values):
    return float(np.sqrt(np.mean(values**2)))


def compare(fem, direct, viscous, reference, force_scale):
    flow, negative, positive, wall_vorticity, wall_shear, wall_velocity = fem.T
    if np.any(wall_velocity != 0):
        raise ValueError("Expected zero wall-velocity contribution for stationary Omega0")
    reconstructed = (flow + wall_vorticity + wall_shear) / force_scale
    error = reconstructed - reference
    rmse = rms(error)
    reference_rms = rms(reference)
    if reference_rms == 0:
        raise ValueError("Reference force coefficient has zero RMS in the comparison window")
    return reconstructed, {
        "reference_mean": float(np.mean(reference)),
        "reconstructed_mean": float(np.mean(reconstructed)),
        "reference_fluctuation_rms": float(np.std(reference)),
        "reconstructed_fluctuation_rms": float(np.std(reconstructed)),
        "bias": float(np.mean(error)),
        "rmse": rmse,
        "max_abs_error": max_abs(error),
        "rmse_over_reference_rms_pct": 100 * rmse / reference_rms,
        "force_coeffs_crosscheck_max_abs": max_abs(direct / force_scale - reference),
        "flow_split_max_abs_N": max_abs(flow - (negative + positive)),
        "wall_velocity_max_abs_N": max_abs(wall_velocity),
        "wall_shear_vs_viscous_rms_N": rms(wall_shear - viscous),
    }


def plot(path, times, histories):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    # Show the final 30 s to resolve phase and amplitude. Metrics use the full window.
    visible = times >= times[-1] - 30
    time = times[visible]
    fig, axes = plt.subplots(
        2, 2, figsize=(10, 5.4), sharex=True, layout="constrained"
    )
    for index, (symbol, reference, reconstructed) in enumerate(histories):
        loads, residuals = axes[:, index]
        loads.plot(time, reference[visible], color="#17567c", label="OpenFOAM forceCoeffs")
        loads.plot(
            time, reconstructed[visible], color="#bf521d", linestyle="--",
            label="Force-element reconstruction",
        )
        loads.set_ylabel(symbol)
        residuals.axhline(0, color="0.55", linewidth=0.8)
        residuals.plot(time, (reconstructed - reference)[visible], color="#73428a")
        residuals.set_ylabel(f"Δ{symbol}")
        residuals.set_xlabel("Time [s]")
        for axis in (loads, residuals):
            axis.grid(alpha=0.23)
            axis.set_xlim(time[0], time[-1])
    axes[0, 0].legend(loc="best", fontsize=8)
    fig.suptitle("Stationary cylinder · Ω = 0 · Re = 150")
    fig.savefig(path, dpi=160)
    plt.close(fig)


def validate(case, output, start, end):
    if not math.isfinite(start) or not math.isfinite(end) or start < DT or start >= end:
        raise ValueError(f"Expected a finite time window with {DT:g} <= start < end")
    first_step = math.ceil((start - TIME_ATOL) / DT)
    last_step = math.floor((end + TIME_ATOL) / DT)
    if not first_step < last_step <= round(800 / DT):
        raise ValueError("Expected at least two time steps in the saved range up to 800 s")
    steps = np.arange(first_step, last_step + 1)
    times = steps * DT
    paths = {name: case / relative for name, relative in INPUTS.items()}
    figure = output / "omega0-force-closure.png"
    report = output / "omega0-summary.json"
    for path in (output, figure, report):
        if path.resolve().is_relative_to(case):
            raise ValueError("The output directory and files must be outside the source case")

    force_rows, _ = read_openfoam_table(
        paths["forces"], ("Time", "total_x", "total_y", "viscous_x", "viscous_y")
    )
    direct = select_steps(paths["forces"], force_rows, steps)
    coeff_rows, metadata = read_openfoam_table(paths["coefficients"], ("Time", "Cd", "Cl"))
    coefficients = select_steps(paths["coefficients"], coeff_rows, steps)
    for name, expected in (("dragDir", [1, 0, 0]), ("liftDir", [0, 1, 0])):
        direction = [float(value) for value in metadata[name].strip("()").split()]
        if direction != expected:
            raise ValueError(f"Expected {name} = {expected} in coefficient.dat")
    speed, area = (float(metadata[name]) for name in ("magUInf", "Aref"))
    if not all(math.isfinite(value) and value > 0 for value in (speed, area)):
        raise ValueError("Expected positive finite magUInf and Aref in coefficient.dat")
    force_scale = 0.5 * RHO * speed**2 * area

    summary = {
        "case": "T0/Omega0",
        "time_s": {"first": float(times[0]), "last": float(times[-1]), "step": DT},
        "sample_count": len(times),
        "rho_kg_m3": RHO,
        "magUInf_m_s": speed,
        "Aref_m2": area,
        "force_scale_N": force_scale,
        "inputs_sha256": {},
        "metrics": {},
    }
    histories = []
    for index, (name, symbol) in enumerate((("drag", "Cd"), ("lift", "Cl"))):
        fem = select_steps(paths[name], read_fem(paths[name]), steps)
        reference = coefficients[:, index]
        reconstructed, metrics = compare(
            fem, direct[:, index], direct[:, index + 2], reference, force_scale
        )
        summary["metrics"][symbol] = metrics
        histories.append((symbol, reference, reconstructed))
    for path in paths.values():
        with path.open("rb") as stream:
            relative_path = path.relative_to(case).as_posix()
            summary["inputs_sha256"][relative_path] = hashlib.file_digest(stream, "sha256").hexdigest()

    output.mkdir(parents=True, exist_ok=True)
    plot(figure, times, histories)
    report.write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", type=Path, required=True, help="Path to the Omega0 case")
    parser.add_argument("--output", type=Path, required=True, help="Output directory outside the case")
    parser.add_argument("--start", type=float, default=700, help="Start time [s], inclusive")
    parser.add_argument("--end", type=float, default=800, help="End time [s], inclusive")
    args = parser.parse_args()
    try:
        report = validate(args.case.resolve(strict=True), args.output.resolve(), args.start, args.end)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")
    print(report)


if __name__ == "__main__":
    main()
