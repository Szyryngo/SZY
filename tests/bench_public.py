#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_public.py (v3 - real public datasets + logi, padding, tabele i wykresy)

Benchmark SZY/SZ3/ZFP/Zstd/Blosc2 na publicznych/synthetic danych 2D.
Automatycznie:
  - ściąga wybrane publiczne dane (NetCDF / GeoTIFF / CSV / ZIP),
  - paddinguje do wielokrotności block_size,
  - drukuje logi, tabele i generuje wykresy PNG.

Datasety (częściowo publiczne, częściowo syntetyczne):
  - climate:
      * clim_avhrr_sst_1981-09-01  (NetCDF, AVHRR SST, Unidata/netcdf4-python)
      * clim_placeholder_temp_720x1440  (synthetic)
  - satellite:
      * sat_landsat_L8_B4_001002_20160816 (GeoTIFF sample z repo Rasterio, band 1)
      * sat_placeholder_2048x2048         (synthetic)
  - sensors:
      * sensor_temp_daily_min  (CSV, jbrownlee)
      * sensor_airquality_uci  (ZIP+CSV, UCI)
      * sensor_energy_uci      (CSV, UCI)
  - medical/astronomy:
      * med_placeholder_ct_512x512   (synthetic)
      * astro_placeholder_ccd_2048x2048 (synthetic)
  - simulation:
      * sim_ocean_temp_slice (NetCDF, ocean_his_0001 z Unidata/netcdf4-python)

Wymagane:
  - bench_common.py w tym samym katalogu,
  - opcjonalnie: netCDF4, rasterio, pandas, matplotlib
    (jeśli brak, odpowiednie testy są pomijane).
    
    
    python bench_public.py --dll szytool.dll --outdir out_public --mode all --eps 5,10,20 --block 64,128
    
"""

import os
import sys
import json
import time
import math
import urllib.request
import zipfile
from dataclasses import dataclass, asdict
from typing import List, Dict, Any, Tuple, Optional

import numpy as np

try:
    import pandas as pd
except Exception:
    pd = None

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAS_MPL = True
except Exception:
    HAS_MPL = False

# opcjonalne biblioteki do NetCDF / GeoTIFF
try:
    import netCDF4
except Exception:
    netCDF4 = None

try:
    import rasterio
except Exception:
    rasterio = None

# import wspólnych narzędzi
from bench_common import (
    SzyDLL,
    ensure_dir,
    ensure_contig,
    now_iso,
    sha256_file,
    dataset_stats_f64,
    benchmark_dataset,
)


# ==========================
# Download helper
# ==========================

def download_file(url: str, cache_dir: str, filename: Optional[str] = None) -> str:
    """
    Pobiera plik z URL do cache_dir (jeśli jeszcze nie istnieje).
    Zwraca pełną ścieżkę do pliku.
    """
    ensure_dir(cache_dir)
    if filename:
        fname = filename
    else:
        fname = os.path.basename(url.split("?")[0]) or "download.bin"
    out_path = os.path.join(cache_dir, fname)
    if os.path.exists(out_path) and os.path.getsize(out_path) > 0:
        return out_path
    print(f"  [DOWNLOAD] {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "SZY-benchmark"})
    with urllib.request.urlopen(req, timeout=300) as r:
        data = r.read()
    with open(out_path, "wb") as f:
        f.write(data)
    return out_path


# ==========================
# Padding helper
# ==========================

def pad_to_multiple(arr: np.ndarray, mult: int, pad_value=0) -> Tuple[np.ndarray, Tuple[int, int], Tuple[int, int]]:
    if mult <= 1:
        return arr, arr.shape, arr.shape
    H, W = arr.shape
    H2 = ((H + mult - 1) // mult) * mult
    W2 = ((W + mult - 1) // mult) * mult
    if H2 == H and W2 == W:
        return arr, arr.shape, arr.shape
    out = np.full((H2, W2), pad_value, dtype=arr.dtype)
    out[:H, :W] = arr
    return out, (H, W), (H2, W2)


# ==========================
# Dataset specs
# ==========================

@dataclass
class PublicDatasetSpec:
    name: str
    category: str
    url: str
    loader: str
    note: str = ""
    license: str = ""
    citation: str = ""
    dtype: str = "f64"
    extra: Dict[str, Any] = None


# ==========================
# Loadery - sensory
# ==========================

def load_sensor_temperature(cache_dir: str) -> np.ndarray:
    url = "https://raw.githubusercontent.com/jbrownlee/Datasets/master/daily-min-temperatures.csv"
    path = download_file(url, cache_dir)
    if pd is None:
        raise RuntimeError("pandas required for CSV loading")
    df = pd.read_csv(path)
    if "Temp" not in df.columns:
        raise RuntimeError("CSV missing 'Temp' column")
    temps = df["Temp"].astype(float).to_numpy()
    L = len(temps)
    H, W = 365, L // 365
    if W == 0:
        return temps.reshape((1, L)).astype(np.float64)
    return temps[:H * W].reshape((H, W)).astype(np.float64)


def load_sensor_airquality(cache_dir: str) -> np.ndarray:
    url = "https://archive.ics.uci.edu/ml/machine-learning-databases/00360/AirQualityUCI.zip"
    path_zip = download_file(url, cache_dir)
    if pd is None:
        raise RuntimeError("pandas required")
    with zipfile.ZipFile(path_zip, "r") as zf:
        csv_name = next((n for n in zf.namelist() if n.lower().endswith(".csv")), None)
        if not csv_name:
            raise RuntimeError("No .csv in zip")
        with zf.open(csv_name) as f:
            df = pd.read_csv(f, sep=";", decimal=",")
    cols = [c for c in df.columns if c.startswith("PT08") or c.startswith("CO(")] or [
        c for c in df.columns if df[c].dtype != object
    ]
    arr = df[cols].dropna().to_numpy(dtype=np.float64)
    return arr[:4096, :16]


def load_sensor_energy(cache_dir: str) -> np.ndarray:
    url = "https://archive.ics.uci.edu/ml/machine-learning-databases/00374/energydata_complete.csv"
    path = download_file(url, cache_dir)
    if pd is None:
        raise RuntimeError("pandas required")
    df = pd.read_csv(path)
    cols = [c for c in df.columns if c.startswith("T") or c == "Appliances"] or [
        c for c in df.columns if df[c].dtype != object
    ]
    arr = df[cols].dropna().to_numpy(dtype=np.float64)
    return arr[:4096, :16]


# ==========================
# Loadery - synthetic placeholders
# ==========================

def load_placeholder_satellite(cache_dir: str) -> np.ndarray:
    H, W = 2048, 2048
    y = np.linspace(0, 65535, H, dtype=np.float64)[:, None]
    x = np.linspace(0, 65535, W, dtype=np.float64)[None, :]
    return ((x + y) / 2.0).astype(np.uint16)


def load_placeholder_medical(cache_dir: str) -> np.ndarray:
    H, W = 512, 512
    a = np.zeros((H, W), dtype=np.float64)
    a[:H // 2, :W // 2] = 200
    a[:H // 2, W // 2:] = 800
    a[H // 2:, :W // 2] = 1200
    a[H // 2:, W // 2:] = 300
    a += np.random.default_rng(123).normal(0, 20, size=(H, W))
    return np.clip(a, 0, 4095).astype(np.uint16)


def load_placeholder_astronomy(cache_dir: str) -> np.ndarray:
    H, W = 2048, 2048
    rng = np.random.default_rng(42)
    sky = rng.normal(1000, 30, size=(H, W))
    for _ in range(500):
        y, x = rng.integers(0, H), rng.integers(0, W)
        amp = rng.exponential(5000)
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                yy, xx = y + dy, x + dx
                if 0 <= yy < H and 0 <= xx < W:
                    sky[yy, xx] += amp * math.exp(-(dy * dy + dx * dx) / 2.0)
    return np.clip(sky, 0, 65535).astype(np.uint16)


def load_placeholder_climate(cache_dir: str) -> np.ndarray:
    H, W = 720, 1440
    lat = np.linspace(-90, 90, H, dtype=np.float64)[:, None]
    lon = np.linspace(-180, 180, W, dtype=np.float64)[None, :]
    return (15 - 30 * np.abs(lat) / 90 + 5 * np.sin(2 * np.pi * lon / 360)).astype(np.float32)


# ==========================
# Loadery - real public NetCDF / GeoTIFF
# ==========================

def load_climate_sst_avhrr(cache_dir: str) -> np.ndarray:
    """
    AVHRR SST NetCDF z repozytorium Unidata/netcdf4-python:
      https://github.com/Unidata/netcdf4-python/raw/main/examples/avhrr-only-v2.19810901.nc

    Zmienna: 'sst', kształt (time, lat, lon)
    Bierzemy time=0 -> 2D (lat, lon), dtype float32.
    """
    if netCDF4 is None:
        raise RuntimeError("netCDF4 required for AVHRR SST (pip install netCDF4)")
    url = "https://github.com/Unidata/netcdf4-python/raw/main/examples/avhrr-only-v2.19810901.nc"
    path = download_file(url, cache_dir, filename="avhrr-only-v2.19810901.nc")
    with netCDF4.Dataset(path, "r") as ds:
        if "sst" not in ds.variables:
            raise RuntimeError("Variable 'sst' not found in AVHRR file")
        v = ds.variables["sst"]
        if v.ndim != 3:
            raise RuntimeError(f"sst ndim={v.ndim}, expected 3 (time, lat, lon)")
        arr = np.array(v[0, ...], dtype=np.float32)
    if arr.ndim != 2:
        raise RuntimeError(f"AVHRR SST slice has shape {arr.shape}, expected 2D")
    return arr


def load_sim_ocean_temp_slice(cache_dir: str) -> np.ndarray:
    """
    Ocean model simulation NetCDF z Unidata/netcdf4-python:
      https://github.com/Unidata/netcdf4-python/raw/main/examples/ocean_his_0001.nc

    Zmienna: 'temp' (ocean temperature).
    Typowy kształt: (ocean_time, s_rho, eta_rho, xi_rho).
    Bierzemy time=0, depth=środek (s_rho // 2) -> 2D (eta, xi), dtype float32.
    """
    if netCDF4 is None:
        raise RuntimeError("netCDF4 required for ocean sim (pip install netCDF4)")
    url = "https://github.com/Unidata/netcdf4-python/raw/main/examples/ocean_his_0001.nc"
    path = download_file(url, cache_dir, filename="ocean_his_0001.nc")
    with netCDF4.Dataset(path, "r") as ds:
        if "temp" not in ds.variables:
            raise RuntimeError("Variable 'temp' not found in ocean_his_0001.nc")
        v = ds.variables["temp"]
        if v.ndim < 3:
            raise RuntimeError(f"temp ndim={v.ndim}, expected >=3")
        if v.ndim == 3:
            # (time, y, x)
            arr = np.array(v[0, ...], dtype=np.float32)
        else:
            # (time, depth, y, x)
            depth_mid = v.shape[1] // 2
            arr = np.array(v[0, depth_mid, ...], dtype=np.float32)
    if arr.ndim != 2:
        raise RuntimeError(f"Ocean temp slice has shape {arr.shape}, expected 2D")
    return arr


def load_satellite_landsat_band(cache_dir: str) -> np.ndarray:
    """
    Real GeoTIFF sample (RGB) z repozytorium Rasterio:
      https://github.com/rasterio/rasterio/raw/main/tests/data/RGB.byte.tif

    To nie jest dokładnie Landsat, ale prawdziwy obraz z trzema pasmami,
    dobry jako publiczny test satelitarny. Używamy bandu 1 i rzutujemy do uint16.
    """
    if rasterio is None:
        raise RuntimeError("rasterio required for GeoTIFF loader (pip install rasterio)")

    url = "https://github.com/rasterio/rasterio/raw/main/tests/data/RGB.byte.tif"
    path = download_file(url, cache_dir, filename="RGB.byte.tif")

    with rasterio.open(path) as ds:
        arr = ds.read(1)  # band 1, uint8

    return np.asarray(arr, dtype=np.uint16)


# ==========================
# get_public_specs
# ==========================

def get_public_specs(mode: str) -> List[PublicDatasetSpec]:
    specs: List[PublicDatasetSpec] = []

    # SATELLITE
    if mode in ("all", "satellite"):
        specs.append(PublicDatasetSpec(
            "sat_landsat_L8_B4_001002_20160816",
            "satellite",
            "RGB.byte.tif (Rasterio sample GeoTIFF)",
            "load_satellite_landsat_band",
            dtype="u16",
        ))
        specs.append(PublicDatasetSpec(
            "sat_placeholder_2048x2048",
            "satellite",
            "(synthetic gradient)",
            "load_placeholder_satellite",
            dtype="u16",
        ))

    # MEDICAL
    if mode in ("all", "medical"):
        specs.append(PublicDatasetSpec(
            "med_placeholder_ct_512x512",
            "medical",
            "(synthetic CT-like)",
            "load_placeholder_medical",
            dtype="u16",
        ))

    # ASTRONOMY
    if mode in ("all", "astronomy"):
        specs.append(PublicDatasetSpec(
            "astro_placeholder_ccd_2048x2048",
            "astronomy",
            "(synthetic CCD-like)",
            "load_placeholder_astronomy",
            dtype="u16",
        ))

    # CLIMATE
    if mode in ("all", "climate"):
        specs.append(PublicDatasetSpec(
            "clim_avhrr_sst_1981-09-01",
            "climate",
            "AVHRR-only SST (NetCDF, Unidata/netcdf4-python)",
            "load_climate_sst_avhrr",
            dtype="f32",
        ))
        specs.append(PublicDatasetSpec(
            "clim_placeholder_temp_720x1440",
            "climate",
            "(synthetic temperature field)",
            "load_placeholder_climate",
            dtype="f32",
        ))

    # SENSORS
    if mode in ("all", "sensors"):
        specs.append(PublicDatasetSpec(
            "sensor_temp_daily_min",
            "sensor",
            "jbrownlee daily-min-temperatures",
            "load_sensor_temperature",
            dtype="f64",
        ))
        specs.append(PublicDatasetSpec(
            "sensor_airquality_uci",
            "sensor",
            "UCI AirQuality",
            "load_sensor_airquality",
            dtype="f64",
        ))
        specs.append(PublicDatasetSpec(
            "sensor_energy_uci",
            "sensor",
            "UCI Energy",
            "load_sensor_energy",
            dtype="f64",
        ))

    # SIMULATION (ocean model)
    if mode in ("all", "sim"):
        specs.append(PublicDatasetSpec(
            "sim_ocean_temp_slice",
            "simulation",
            "ROMS ocean temp slice (NetCDF, Unidata/netcdf4-python)",
            "load_sim_ocean_temp_slice",
            dtype="f32",
        ))

    return specs


# ==========================
# Console table & plots
# ==========================

def print_console_summary(rows: List[Dict[str, Any]]):
    print("\n" + "=" * 90)
    print("BENCHMARK SUMMARY")
    print("=" * 90)
    print(f"{'Dataset':<35} {'Codec':<15} {'eps':>5} {'B':>4} "
          f"{'Ratio':>7} {'MaxErr':>7} {'Enc(ms)':>7} {'Dec(ms)':>7}")
    print("-" * 90)
    ok_rows = [r for r in rows if "ratio" in r and not r.get("error")]
    for r in sorted(ok_rows, key=lambda x: (x["dataset"], x["codec"], x["eps"], x["block"])):
        print(f"{r['dataset']:<35} {r['codec']:<15} {r['eps']:>5.1f} {r['block']:>4} "
              f"{r['ratio']:>7.2f}x {r.get('max_err','?'):>7.2f} "
              f"{r.get('enc_ms','?'):>7.1f} {r.get('dec_ms','?'):>7.1f}")
    print("=" * 90)


def generate_plots(rows: List[Dict[str, Any]], outdir: str):
    if not HAS_MPL or pd is None:
        print("[WARN] matplotlib or pandas not installed. Skipping plots.")
        return
    plotdir = os.path.join(outdir, "plots")
    ensure_dir(plotdir)

    ok = [r for r in rows if "ratio" in r and not r.get("error")]
    if not ok:
        return

    df = pd.DataFrame(ok)
    for ds, g in df.groupby("dataset"):
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))

        # Ratio per codec
        g_ratio = g.sort_values("ratio", ascending=False)
        axes[0].bar(g_ratio["codec"], g_ratio["ratio"])
        axes[0].set_title(f"Ratio - {ds}")
        axes[0].set_ylabel("Compression Ratio")
        axes[0].tick_params(axis='x', rotation=45)
        for label in axes[0].get_xticklabels():
            label.set_ha('right')

        # MaxErr per codec
        g_err = g.sort_values("max_err", ascending=False)
        axes[1].bar(g_err["codec"], g_err["max_err"], color='orange')
        axes[1].set_title(f"Max Error - {ds}")
        axes[1].set_ylabel("Max Absolute Error")
        axes[1].tick_params(axis='x', rotation=45)
        for label in axes[1].get_xticklabels():
            label.set_ha('right')

        plt.tight_layout()
        fname = f"summary_{ds.replace(' ', '_')}.png"
        plt.savefig(os.path.join(plotdir, fname), dpi=150)
        plt.close()
    print(f"[PLOTS] Saved to {plotdir}/")


# ==========================
# Main
# ==========================

def main():
    import argparse
    ap = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--dll", required=True, help="Ścieżka do szytool.dll / .so")
    ap.add_argument("--outdir", default="out_public", help="Katalog wyjściowy")
    ap.add_argument("--mode", default="all",
                    choices=["all", "satellite", "medical", "astronomy", "climate", "sensors", "sim"],
                    help="Kategoria datasetów do testu")
    ap.add_argument("--repeat", type=int, default=3, help="Powtórzenia do mediany czasu")
    ap.add_argument("--eps", default="5,10,20", help="Lista eps, np. '5,10,20'")
    ap.add_argument("--block", default="64,128", help="Lista block sizes, np. '64,128'")
    ap.add_argument("--szy-threads", type=int, default=0, help="Wątki dla SZY (0=auto, 1=single, >1=strict)")
    args = ap.parse_args()

    ensure_dir(args.outdir)
    dll = SzyDLL(args.dll)
    hw = dll.hw_threads()
    print(f"[bench_public] dll={os.path.abspath(args.dll)} | hw_threads={hw} | mode={args.mode}")

    eps_list = [float(s) for s in args.eps.split(",") if s.strip()]
    block_list = [int(s) for s in args.block.split(",") if s.strip()]
    align = max(block_list)  # padding do największego bloku

    specs = get_public_specs(args.mode)
    meta = {
        "timestamp": now_iso(),
        "dll": os.path.abspath(args.dll),
        "hw": hw,
        "mode": args.mode,
        "eps": eps_list,
        "block": block_list,
        "datasets": [],
        "errors": []
    }
    all_rows: List[Dict[str, Any]] = []
    cache_dir = os.path.join(args.outdir, "_cache")

    for spec in specs:
        print(f"\n{'=' * 60}\n[DATASET] {spec.category}:{spec.name}")
        try:
            loader = globals().get(spec.loader)
            if not loader:
                raise RuntimeError(f"Missing loader: {spec.loader}")

            arr = ensure_contig(loader(cache_dir))
            H0, W0 = arr.shape
            arr, (H0, W0), (H2, W2) = pad_to_multiple(arr, align)
            if (H0, W0) != (H2, W2):
                print(f"  [PAD] {H0}x{W0} -> {H2}x{W2} (align={align})")

            print(f"  shape={H2}x{W2}, dtype={arr.dtype}, url={spec.url}")
            meta["datasets"].append({
                **asdict(spec),
                "H_orig": H0,
                "W_orig": W0,
                "H_pad": H2,
                "W_pad": W2
            })

            rows = benchmark_dataset(
                dll, spec.name, arr,
                eps_list=eps_list,
                block_list=block_list,
                szy_threads=args.szy_threads,
                szy_par_min_tiles=64,
                zfp_tols=[e * 0.5 for e in eps_list],
                repeat=args.repeat
            )
            all_rows.extend(rows)

            # Krótki podgląd SZY
            szy_ok = [r for r in rows if r.get("codec") == "SZY_v8" and "ratio" in r]
            if szy_ok:
                best = max(szy_ok, key=lambda r: r["ratio"])
                print(f"  [SZY BEST] eps={best['eps']} B={best['block']} "
                      f"ratio={best['ratio']:.2f}x max_err={best['max_err']:.2f}")

        except Exception as e:
            print(f"  [ERROR] {e}")
            meta["errors"].append({"dataset": spec.name, "error": str(e)})

    print_console_summary(all_rows)
    generate_plots(all_rows, args.outdir)

    # Zapis JSON/CSV
    out_json = os.path.join(args.outdir, "compare_all.json")
    out_csv = os.path.join(args.outdir, "compare_all.csv")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump({"meta": meta, "rows": all_rows}, f, indent=2, ensure_ascii=False)
    if pd is not None:
        pd.DataFrame(all_rows).to_csv(out_csv, index=False)

    # Bogatszy report.md
    report = os.path.join(args.outdir, "report.md")
    with open(report, "w", encoding="utf-8") as f:
        f.write("# Public Benchmark Report\n\n")
        f.write(f"Generated: `{meta['timestamp']}`\n\n")
        f.write("## Datasets\n\n")
        for ds in meta["datasets"]:
            f.write(f"- **{ds['category']}:{ds['name']}** "
                    f"`{ds['H_pad']}x{ds['W_pad']}` ({ds['dtype']}) "
                    f" — {ds['url']}\n")
        f.write("\n## Results\n\n")
        f.write("See `compare_all.csv` for full data.\n")
        f.write("See `plots/` for visual comparisons.\n")
    print(f"\nDONE. Files saved to: {args.outdir}/")


if __name__ == "__main__":
    main()