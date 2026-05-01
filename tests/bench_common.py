#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_common.py

Wspólne narzędzia do benchmarkowania kodeków:
- SZY_v8 (DLL)
- SZ3 (pysz)
- ZFP (zfpy)
- Zstd (lossless)
- Blosc2 (lossless)

Uwaga:
- Wszystkie zależności typu zfpy, pysz, zstandard, blosc2 są opcjonalne.
  Jeśli nie są zainstalowane, odpowiadający kodek jest po prostu pomijany.
"""

import os
import sys
import time
import json
import hashlib
from typing import Dict, Any, List, Tuple, Optional

import numpy as np
import ctypes


# ===========
# Utilities
# ===========

def now_iso() -> str:
    import datetime
    dt = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
    return dt.isoformat().replace("+00:00", "Z")


def sha256_file(path: str, chunk: int = 8 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def ensure_dir(p: str):
    os.makedirs(p, exist_ok=True)


def ensure_contig(x: np.ndarray) -> np.ndarray:
    return x if x.flags["C_CONTIGUOUS"] else np.ascontiguousarray(x)


def ratio_from_bytes(raw_bytes: int, comp_bytes: int) -> float:
    return float(raw_bytes) / float(max(comp_bytes, 1))


def bpp_from_bytes(nbytes: int, H: int, W: int) -> float:
    return (8.0 * float(nbytes)) / float(max(H * W, 1))


def dataset_stats_f64(x: np.ndarray) -> Dict[str, float]:
    a = np.asarray(x, dtype=np.float64)
    if a.size == 0:
        return {"min": 0.0, "max": 0.0, "mean": 0.0, "std": 0.0, "p01": 0.0, "p99": 0.0}
    mn = float(np.min(a))
    mx = float(np.max(a))
    mean = float(np.mean(a))
    std = float(np.std(a))
    p01 = float(np.percentile(a, 1.0))
    p99 = float(np.percentile(a, 99.0))
    return {"min": mn, "max": mx, "mean": mean, "std": std, "p01": p01, "p99": p99}


def stream_metrics_abs_err(orig: np.ndarray, rec: np.ndarray) -> Dict[str, float]:
    """Liczy max_err, RMSE, mean_err, p99_err na całym obrazie."""
    if orig.shape != rec.shape:
        raise ValueError(f"Shape mismatch: orig={orig.shape}, rec={rec.shape}")
    o = np.asarray(orig, dtype=np.float64)
    r = np.asarray(rec, dtype=np.float64)
    e = o - r
    ae = np.abs(e)

    if not np.isfinite(ae).all():
        return {"max_err": float("inf"), "rmse": float("inf"), "mean_err": float("inf"), "p99_err": float("inf")}

    max_err = float(ae.max()) if ae.size else 0.0
    rmse = float(np.sqrt(np.mean(e*e))) if e.size else 0.0
    mean_err = float(np.mean(e)) if e.size else 0.0
    p99 = float(np.percentile(ae, 99.0)) if ae.size else 0.0
    return {"max_err": max_err, "rmse": rmse, "mean_err": mean_err, "p99_err": p99}


# ===========
# SZY DLL wrapper
# ===========

class SzyDLL:
    """Wrapper dla szytool.dll."""
    def __init__(self, dll_path: str):
        self.dll_path = os.path.abspath(dll_path)
        self.lib = ctypes.CDLL(self.dll_path)

        # free
        self.lib.szy_free_buffer.argtypes = [ctypes.c_void_p]
        self.lib.szy_free_buffer.restype = None

        # strerror
        self.lib.szy_strerror.argtypes = [ctypes.c_int]
        self.lib.szy_strerror.restype = ctypes.c_char_p

        # hw threads
        self.lib.szy_hw_threads.argtypes = []
        self.lib.szy_hw_threads.restype = ctypes.c_int

        # compress double
        self.lib.szy_compress_buffer_ex.argtypes = [
            np.ctypeslib.ndpointer(dtype=np.float64, ndim=2, flags="C_CONTIGUOUS"),
            ctypes.c_int, ctypes.c_int,
            ctypes.c_double, ctypes.c_int, ctypes.c_int,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.szy_compress_buffer_ex.restype = ctypes.c_int

        # compress f32
        self.lib.szy_compress_f32_ex.argtypes = [
            np.ctypeslib.ndpointer(dtype=np.float32, ndim=2, flags="C_CONTIGUOUS"),
            ctypes.c_int, ctypes.c_int,
            ctypes.c_float, ctypes.c_int, ctypes.c_int,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.szy_compress_f32_ex.restype = ctypes.c_int

        # compress u16
        self.lib.szy_compress_u16_ex.argtypes = [
            np.ctypeslib.ndpointer(dtype=np.uint16, ndim=2, flags="C_CONTIGUOUS"),
            ctypes.c_int, ctypes.c_int,
            ctypes.c_double, ctypes.c_int, ctypes.c_int,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.szy_compress_u16_ex.restype = ctypes.c_int

        # decompress_into f64
        self.lib.szy_decompress_into_buffer_ex2.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            np.ctypeslib.ndpointer(dtype=np.float64, ndim=2, flags="C_CONTIGUOUS"),
            ctypes.c_size_t,
            ctypes.c_int, ctypes.c_int,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)
        ]
        self.lib.szy_decompress_into_buffer_ex2.restype = ctypes.c_int

    def strerror(self, rc: int) -> str:
        p = self.lib.szy_strerror(int(rc))
        return p.decode("utf-8", errors="replace") if p else "<null>"

    def hw_threads(self) -> int:
        return max(int(self.lib.szy_hw_threads()), 1)

    def compress(self, x: np.ndarray, eps: float, block: int, num_workers: int) -> bytes:
        x = ensure_contig(x)
        H, W = x.shape
        out_ptr = ctypes.POINTER(ctypes.c_uint8)()
        out_n = ctypes.c_size_t(0)

        if x.dtype == np.float64:
            rc = self.lib.szy_compress_buffer_ex(
                x, int(H), int(W),
                float(eps), int(block), int(num_workers),
                ctypes.byref(out_ptr), ctypes.byref(out_n)
            )
        elif x.dtype == np.float32:
            rc = self.lib.szy_compress_f32_ex(
                x, int(H), int(W),
                float(eps), int(block), int(num_workers),
                ctypes.byref(out_ptr), ctypes.byref(out_n)
            )
        elif x.dtype == np.uint16:
            rc = self.lib.szy_compress_u16_ex(
                x, int(H), int(W),
                float(eps), int(block), int(num_workers),
                ctypes.byref(out_ptr), ctypes.byref(out_n)
            )
        else:
            raise ValueError(f"SZY: unsupported dtype {x.dtype}")

        if rc != 0:
            raise RuntimeError(f"SZY compress rc={rc}: {self.strerror(rc)}")

        try:
            blob = ctypes.string_at(out_ptr, out_n.value)
        finally:
            self.lib.szy_free_buffer(out_ptr)

        return blob

    def decompress_into_f64(self, blob: bytes, out: np.ndarray, num_workers: int, parallel_min_tiles: int) -> Tuple[int, int]:
        if out.dtype != np.float64 or not out.flags["C_CONTIGUOUS"]:
            raise ValueError("SZY decompress_into_f64: out must be float64 C_CONTIGUOUS")

        buf = (ctypes.c_uint8 * len(blob)).from_buffer_copy(blob)
        H = ctypes.c_int(0); W = ctypes.c_int(0)

        rc = self.lib.szy_decompress_into_buffer_ex2(
            ctypes.cast(buf, ctypes.POINTER(ctypes.c_uint8)),
            ctypes.c_size_t(len(blob)),
            out, ctypes.c_size_t(out.size),
            ctypes.c_int(int(num_workers)),
            ctypes.c_int(int(parallel_min_tiles)),
            ctypes.byref(H), ctypes.byref(W)
        )
        if rc != 0:
            raise RuntimeError(f"SZY decompress rc={rc}: {self.strerror(rc)}")

        return H.value, W.value


# ===========
# Other codecs
# ===========

def codec_supported(codec: str, dtype: np.dtype) -> bool:
    if codec == "SZY_v8":
        return dtype in (np.float64, np.float32, np.uint16)
    if codec == "SZ3_pysz":
        return dtype in (np.float64, np.float32)
    if codec == "ZFP_zfpy":
        return dtype in (np.float64, np.float32)
    if codec in ("Zstd_lossless", "Blosc2_lossless"):
        return True
    return False


def zstd_lossless_bytes(data: bytes, level: int = 3) -> Tuple[int, float, float]:
    try:
        import zstandard as zstd
    except ImportError:
        raise RuntimeError("zstandard (python zstd) not installed")

    raw_hash = hashlib.sha256(data).hexdigest()

    t0 = time.perf_counter()
    cctx = zstd.ZstdCompressor(level=level)
    comp = cctx.compress(data)
    t1 = time.perf_counter()

    t2 = time.perf_counter()
    dctx = zstd.ZstdDecompressor()
    dec = dctx.decompress(comp)
    t3 = time.perf_counter()

    if hashlib.sha256(dec).hexdigest() != raw_hash:
        raise RuntimeError("Zstd: decompressed data SHA mismatch")

    return len(comp), (t1 - t0), (t3 - t2)


def blosc2_lossless_bytes(data: bytes, cname: str = "zstd", clevel: int = 5) -> Tuple[int, float, float]:
    try:
        import blosc2
    except ImportError:
        raise RuntimeError("blosc2 not installed")

    raw_hash = hashlib.sha256(data).hexdigest()
    codec = getattr(getattr(blosc2, "Codec", object()), cname.upper(), None)
    if codec is None and hasattr(blosc2, "Codec"):
        codec = blosc2.Codec.ZSTD

    t0 = time.perf_counter()
    comp = blosc2.compress2(data, codec=codec, clevel=int(clevel), typesize=1)
    t1 = time.perf_counter()

    t2 = time.perf_counter()
    dec = blosc2.decompress2(comp)
    t3 = time.perf_counter()

    if hashlib.sha256(dec).hexdigest() != raw_hash:
        raise RuntimeError("Blosc2: decompressed data SHA mismatch")

    return len(comp), (t1 - t0), (t3 - t2)


def sz3_abs(x_f64: np.ndarray, eps: float) -> Tuple[int, float, float, float, np.ndarray]:
    """
    Zwraca:
      comp_bytes, enc_s, dec_s, ratio_api, y
    """
    try:
        from pysz import sz, szConfig, szErrorBoundMode
    except ImportError:
        raise RuntimeError("pysz (SZ3) not installed")

    cfg = szConfig()
    cfg.errorBoundMode = szErrorBoundMode.ABS
    cfg.absErrorBound = float(eps)

    t0 = time.perf_counter()
    compressed_u8, ratio_api = sz.compress(x_f64, cfg)
    t1 = time.perf_counter()

    compressed_u8 = np.asarray(compressed_u8, dtype=np.uint8)
    comp_bytes = int(compressed_u8.nbytes)

    t2 = time.perf_counter()
    y, _ = sz.decompress(compressed_u8, np.float64, x_f64.shape)
    t3 = time.perf_counter()

    y = np.asarray(y, dtype=np.float64)
    return comp_bytes, (t1 - t0), (t3 - t2), float(ratio_api), y


def zfp_tol(x_f64: np.ndarray, tol: float) -> Tuple[int, float, float, np.ndarray]:
    """
    Zwraca:
      nbytes, enc_s, dec_s, y
    """
    try:
        import zfpy
    except ImportError:
        raise RuntimeError("zfpy (ZFP) not installed")

    t0 = time.perf_counter()
    blob = zfpy.compress_numpy(x_f64, tolerance=float(tol))
    t1 = time.perf_counter()

    t2 = time.perf_counter()
    y = zfpy.decompress_numpy(blob)
    t3 = time.perf_counter()

    y = np.asarray(y, dtype=np.float64)
    return len(blob), (t1 - t0), (t3 - t2), y


# ===========
# Benchmark jednego datasetu
# ===========

def benchmark_dataset(
    dll: SzyDLL,
    name: str,
    arr: np.ndarray,
    *,
    eps_list: List[float],
    block_list: List[int],
    szy_threads: int = 0,
    szy_par_min_tiles: int = 64,
    zfp_tols: List[float] = None,
    repeat: int = 3,
) -> List[Dict[str, Any]]:
    """
    Wykonuje benchmark wszystkich kodeków dla jednego datasetu (arr: 2D numpy).
    Zwraca listę wierszy (dict) z metrykami.
    """
    if zfp_tols is None:
        zfp_tols = [1.0, 2.0, 5.0, 10.0, 20.0]

    arr = ensure_contig(arr)
    H, W = arr.shape
    raw_bytes = int(arr.nbytes)
    ref_f64 = np.asarray(arr, dtype=np.float64)

    rows: List[Dict[str, Any]] = []

    for eps in eps_list:
        for B in block_list:
            if (H % B) != 0 or (W % B) != 0:
                rows.append({
                    "dataset": name,
                    "eps": float(eps),
                    "block": int(B),
                    "codec": "SZY_v8",
                    "error": f"skip: dims {H}x{W} not divisible by B={B}",
                })
                continue

            common = {
                "dataset": name,
                "H": int(H),
                "W": int(W),
                "dtype": str(arr.dtype),
                "raw_bytes": raw_bytes,
                "eps": float(eps),
                "block": int(B),
            }

            # ---- SZY ----
            if codec_supported("SZY_v8", arr.dtype):
                try:
                    enc_times = []
                    blob = None
                    for _ in range(repeat):
                        t0 = time.perf_counter()
                        blob = dll.compress(arr, eps=float(eps), block=int(B), num_workers=int(szy_threads))
                        t1 = time.perf_counter()
                        enc_times.append(t1 - t0)
                    enc_ms = 1000.0 * float(sorted(enc_times)[len(enc_times)//2])
                    size_bytes = len(blob)
                    ratio = ratio_from_bytes(raw_bytes, size_bytes)
                    bpp = bpp_from_bytes(size_bytes, H, W)

                    out = np.empty((H, W), dtype=np.float64)
                    dec_times = []
                    for _ in range(repeat):
                        t2 = time.perf_counter()
                        dll.decompress_into_f64(blob, out, num_workers=int(szy_threads), parallel_min_tiles=int(szy_par_min_tiles))
                        t3 = time.perf_counter()
                        dec_times.append(t3 - t2)
                    dec_ms = 1000.0 * float(sorted(dec_times)[len(dec_times)//2])

                    m = stream_metrics_abs_err(ref_f64, out)
                    max_ok = bool(m["max_err"] <= eps * 1.02 + 1e-9)

                    rows.append({
                        **common,
                        "codec": "SZY_v8",
                        "variant": f"eps={eps:g},B={B},thr={szy_threads}",
                        "size_bytes": size_bytes,
                        "ratio": ratio,
                        "bpp": bpp,
                        "enc_ms": enc_ms,
                        "dec_ms": dec_ms,
                        "max_err": m["max_err"],
                        "rmse": m["rmse"],
                        "mean_err": m["mean_err"],
                        "p99_err": m["p99_err"],
                        "max_err_ok": max_ok,
                    })
                except Exception as e:
                    rows.append({
                        **common,
                        "codec": "SZY_v8",
                        "variant": f"eps={eps:g},B={B},thr={szy_threads}",
                        "error": str(e),
                    })

            # ---- SZ3 ----
            if codec_supported("SZ3_pysz", arr.dtype):
                try:
                    x_f64 = ref_f64
                    enc_times = []
                    dec_times = []
                    comp_bytes_last = None
                    y_last = None
                    ratio_api_last = None

                    for _ in range(repeat):
                        comp_bytes, enc_s, dec_s, ratio_api, y = sz3_abs(x_f64, eps=float(eps))
                        enc_times.append(enc_s)
                        dec_times.append(dec_s)
                        comp_bytes_last = comp_bytes
                        y_last = y
                        ratio_api_last = ratio_api

                    enc_ms = 1000.0 * float(sorted(enc_times)[len(enc_times)//2])
                    dec_ms = 1000.0 * float(sorted(dec_times)[len(dec_times)//2])

                    m = stream_metrics_abs_err(ref_f64, y_last)
                    max_ok = bool(m["max_err"] <= eps * 1.02 + 1e-9)

                    rows.append({
                        **common,
                        "codec": "SZ3_pysz",
                        "variant": f"ABS eps={eps:g}",
                        "size_bytes": int(comp_bytes_last),
                        "ratio": ratio_from_bytes(raw_bytes, int(comp_bytes_last)),
                        "bpp": bpp_from_bytes(int(comp_bytes_last), H, W),
                        "enc_ms": enc_ms,
                        "dec_ms": dec_ms,
                        "max_err": m["max_err"],
                        "rmse": m["rmse"],
                        "mean_err": m["mean_err"],
                        "p99_err": m["p99_err"],
                        "max_err_ok": max_ok,
                        "note": f"ratio_api={ratio_api_last:.3f}",
                    })
                except Exception as e:
                    rows.append({**common, "codec": "SZ3_pysz", "variant": f"ABS eps={eps:g}", "error": str(e)})

            # ---- ZFP ----
            if codec_supported("ZFP_zfpy", arr.dtype):
                try:
                    for tol in zfp_tols:
                        enc_times = []
                        dec_times = []
                        y_last = None
                        nbytes_last = None

                        for _ in range(repeat):
                            nbytes, enc_s, dec_s, y = zfp_tol(ref_f64, float(tol))
                            enc_times.append(enc_s)
                            dec_times.append(dec_s)
                            y_last = y
                            nbytes_last = nbytes

                        enc_ms = 1000.0 * float(sorted(enc_times)[len(enc_times)//2])
                        dec_ms = 1000.0 * float(sorted(dec_times)[len(dec_times)//2])

                        m = stream_metrics_abs_err(ref_f64, y_last)
                        max_ok = bool(m["max_err"] <= eps * 1.02 + 1e-9)

                        rows.append({
                            **common,
                            "codec": "ZFP_zfpy",
                            "variant": f"tol={tol:g}",
                            "size_bytes": int(nbytes_last),
                            "ratio": ratio_from_bytes(raw_bytes, int(nbytes_last)),
                            "bpp": bpp_from_bytes(int(nbytes_last), H, W),
                            "enc_ms": enc_ms,
                            "dec_ms": dec_ms,
                            "max_err": m["max_err"],
                            "rmse": m["rmse"],
                            "mean_err": m["mean_err"],
                            "p99_err": m["p99_err"],
                            "max_err_ok": max_ok,
                        })
                except Exception as e:
                    rows.append({**common, "codec": "ZFP_zfpy", "variant": "tol_sweep", "error": str(e)})

            # ---- lossless baselines ----
            raw_bytes_view = memoryview(arr).tobytes(order="C")

            # Zstd
            try:
                enc_times = []
                dec_times = []
                comp_bytes_last = None
                for _ in range(repeat):
                    comp_bytes, enc_s, dec_s = zstd_lossless_bytes(raw_bytes_view, level=3)
                    enc_times.append(enc_s)
                    dec_times.append(dec_s)
                    comp_bytes_last = comp_bytes
                enc_ms = 1000.0 * float(sorted(enc_times)[len(enc_times)//2])
                dec_ms = 1000.0 * float(sorted(dec_times)[len(dec_times)//2])
                rows.append({
                    **common,
                    "codec": "Zstd_lossless",
                    "variant": "lvl=3",
                    "size_bytes": int(comp_bytes_last),
                    "ratio": ratio_from_bytes(raw_bytes, int(comp_bytes_last)),
                    "bpp": bpp_from_bytes(int(comp_bytes_last), H, W),
                    "enc_ms": enc_ms,
                    "dec_ms": dec_ms,
                    "max_err": 0.0,
                    "rmse": 0.0,
                    "mean_err": 0.0,
                    "p99_err": 0.0,
                    "max_err_ok": True,
                })
            except Exception as e:
                rows.append({**common, "codec": "Zstd_lossless", "variant": "lvl=3", "error": str(e)})

            # Blosc2
            try:
                enc_times = []
                dec_times = []
                comp_bytes_last = None
                for _ in range(repeat):
                    comp_bytes, enc_s, dec_s = blosc2_lossless_bytes(raw_bytes_view, cname="zstd", clevel=5)
                    enc_times.append(enc_s)
                    dec_times.append(dec_s)
                    comp_bytes_last = comp_bytes
                enc_ms = 1000.0 * float(sorted(enc_times)[len(enc_times)//2])
                dec_ms = 1000.0 * float(sorted(dec_times)[len(dec_times)//2])
                rows.append({
                    **common,
                    "codec": "Blosc2_lossless",
                    "variant": "zstd,cl=5",
                    "size_bytes": int(comp_bytes_last),
                    "ratio": ratio_from_bytes(raw_bytes, int(comp_bytes_last)),
                    "bpp": bpp_from_bytes(int(comp_bytes_last), H, W),
                    "enc_ms": enc_ms,
                    "dec_ms": dec_ms,
                    "max_err": 0.0,
                    "rmse": 0.0,
                    "mean_err": 0.0,
                    "p99_err": 0.0,
                    "max_err_ok": True,
                })
            except Exception as e:
                rows.append({**common, "codec": "Blosc2_lossless", "variant": "zstd,cl=5", "error": str(e)})

    return rows
