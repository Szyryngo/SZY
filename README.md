# SZY – Scientific Compression Engine (current module: SZ2D/v8)

**EN:** SZY is a scientific bounded‑error compression engine for 2D data.  
**PL:** SZY to naukowy silnik stratnej kompresji danych 2D z gwarancją błędu.

---

## EN: Overview

SZY is a tile‑based, bounded‑error compression engine for 2D numerical arrays:

**Data types:**

- `float64`
- `float32`
- `uint16` (e.g. satellite / medical / CCD images)

**Error model:**

- absolute error bound (`max |x - x̂| ≤ eps`) in a strict, controllable way

**Container format:**

- `SZ2D/v8` (magic `SZ2D`, version `8`)
- SHA‑256 integrity (data + container)
- Tile index for random access and fast parallel decode

**Implementation:**

- C engine (`src/`, `include/`)
- DLL / `.so` API (`szy_dll.h`)
- Python wrapper for benchmarks (`bench_common.py`, `bench_public.py`)

This repository currently contains the 2D engine (`SZ2D/v8`).  
The architecture is prepared to be extended to 3D/4D modules in future versions.

---

## EN: Key features

### Bounded error (L∞)

- Configurable absolute error `eps` per dataset.
- In practice, SZY aims for `max_err ≈ 0.98 · eps`, and benchmark code explicitly verifies `max_err ≤ eps · 1.02`.

### Tile‑based architecture (B×B)

Independent tiles (e.g. `64×64` or `128×128`) for:

- Local compression/decompression
- Efficient parallelism
- Partial decode / region‑of‑interest

### Strong integrity

- `data_sha`: SHA‑256 of the data section (index + tiles).
- `container_sha`: SHA‑256 of the entire container (except the last 32 bytes).
- Decode fails fast with explicit error codes if SHA mismatch is detected.

### Engine features (not just a raw codec)

- Tile index encoded as varuint lengths → fast computation of tile offsets.
- Multi‑threaded decode and encode across tiles (Windows + POSIX).
- **decode-into API**: decode directly into a user‑allocated buffer, without extra copies.
- **Peek shape**: `szy_peek_shape_v8()` lets you query `H/W/B` without decoding the data.

### Transforms and entropy coding

- Lorenzo2D predictor on quantized values.
- Median‑bias of deltas, optional zigzag coding.
- Byte shuffle for 16‑bit, bitplane shuffle infrastructure (ready in code).
- Entropy coders: `ZSTD`, `ZLIB`, or `NONE`.

---

## EN: Error model and quality contract

SZY is designed as a scientific lossy codec with a clear quality contract:

**Primary model: absolute error bound `eps`**

- Client passes `eps` to the encoder (via config or DLL API).
- Encoder chooses quantization parameters and events so that `max|x - x̂|` stays within the target.

If, for some blocks, the requested bounded‑error cannot be satisfied with the current configuration (e.g. too strict `eps`, limited event budget), the engine:

- **must not** silently violate the bound,
- returns an explicit error code `SZY_E_BOUNDS` (controlled refusal to compress under these settings).

In benchmarks included in this repo, SZY consistently meets the bound with `max_err` very close to `eps` on a wide range of real and synthetic datasets.

---

## EN: Architecture in a nutshell

### Container `SZ2D/v8`

- **Header:**
  - magic `SZ2D`
  - version `8`
  - flags
  - `H`, `W`, block size `B`
  - meta length + optional metadata JSON
- `data_sha` (32 bytes)
- **Data section:**
  - optional tile index (`ntiles`, `tile_length[i]`)
  - concatenated tile records
- `container_sha` (32 bytes)

### Tile record

- Local shape (`th`, `tw`), `qbits`, `qmode`, `ent_id` (`ZSTD` / `ZLIB` / `NONE`), flags (`shuffle16` / `zigzag` / `bitplane`), predictor ID (`Lorenzo2D`).
- Trend parameters (currently `TREND_NONE` in the C encoder; trends are reserved for future versions).
- Quantization parameters: `inv_scale`, `mean`, `clip_rate`, `bias`.
- Events: indices (delta‑coded varuints) + amplitudes (several modes).
- Entropy‑coded payload for tile data.

### Encode pipeline (per tile)

1. Subtract mean, optionally remove rare events (if events enabled and within budget).
2. Quantize to `int8` / `int16` (with clipping statistics).
3. Apply Lorenzo2D predictor → deltas.
4. Compute and subtract median bias, optionally zigzag.
5. (`qbits=16`) byte‑shuffle or bitplane‑shuffle (future).
6. Entropy code via ZSTD/ZLIB.
7. Assemble tile record.

### Decode pipeline (per tile)

1. Parse tile header.
2. Decompress payload.
3. Undo shuffle/bitplane, undo zigzag, re‑apply bias.
4. Invert Lorenzo2D predictor → quantized field.
5. Dequantize and add back mean/trend.
6. Add event amplitudes.

---

## EN: Public benchmarks – summary

All benchmarks use the same Python harness (`bench_public.py`) and compare:

- SZY v8 (DLL)
- SZ3 (`pysz`)
- ZFP (`zfpy`)
- Zstd (lossless baseline)
- Blosc2 (lossless baseline)

Data is real and/or synthetic but publicly reproducible:

- **satellite:**
  - `sat_landsat_L8_B4_001002_20160816` – `RGB.byte.tif` sample from Rasterio (band 1, `uint8→uint16`, `768×896` after padding).
  - `sat_placeholder_2048x2048` – synthetic gradient `uint16`.
- **medical / astronomy:**
  - `med_placeholder_ct_512x512` – synthetic CT‑like `uint16`.
  - `astro_placeholder_ccd_2048x2048` – synthetic CCD‑like `uint16`.
- **climate:**
  - `clim_placeholder_temp_720x1440` – synthetic temperature field (lat/lon, `f32`), padded to `768×1536`.
- **sensors:**
  - `sensor_temp_daily_min` – jbrownlee daily min temperatures (`f64`).
  - `sensor_airquality_uci`, `sensor_energy_uci` – UCI multivariate time series (`f64`).

### Examples (best SZY configuration in each case)

#### Landsat‑like GeoTIFF (`768×896`, `uint16`)

`sat_landsat_L8_B4_...`, `eps=20`, `block=128`:

- **SZY:** ratio ≈ **16.0×**, `max_err ≈ 19.6`, enc ≈ **9.7 ms**, dec ≈ **8.8 ms**
- **Zstd (lossless):** ≈ **4.38×**
- **Blosc2 (lossless):** ≈ **4.50×**

---

#### Synthetic climate (`768×1536`, `float32`)

`clim_placeholder_temp_720x1440` (padded), `eps=10`, `block=128`:

- **SZY:** ratio ≈ **1041×**, `max_err ≈ 9.8`
- **SZ3:** ratio ≈ **4132×**, `max_err ≈ 9.9`
- **ZFP:** ratio ≈ **24×** (with much smaller error than `eps`)
- **Zstd:** ≈ **1.68×** (lossless)

---

#### UCI Energy dataset (`4096×128`, `float64`)

`sensor_energy_uci`, `eps=20`, `block=128`:

- **SZY:** ratio ≈ **532×**, `max_err ≈ 19.6`
- **SZ3:** ratio ≈ **470×**, `max_err ≈ 20.0`
- **ZFP:** ratio ≈ **126×**, `max_err ≈ 2.74`
- **Zstd:** ratio ≈ **38×** (lossless)

---

#### UCI AirQuality dataset (`4096×128`, `float64`)

`sensor_airquality_uci`, `eps=20`, `block=128`:

- **SZY:** ratio ≈ **196×**, `max_err ≈ 19.6`
- **SZ3:** ratio ≈ **206×**, `max_err ≈ 20.0`
- **ZFP:** ratio ≈ **91×**, `max_err ≈ 3.06`
- **Zstd:** ratio ≈ **60×** (lossless)

---

#### Synthetic CT (`512×512`, `uint16`)

`med_placeholder_ct_512x512`, `eps=20`, `block=128`:

- **SZY:** ratio ≈ **6.0×**, `max_err ≈ 19.6`
- **Zstd:** ≈ **1.84×**
- **Blosc2:** ≈ **1.75×**

On these benchmarks SZY:

- Consistently keeps `max_err` within the configured `eps`.
- Often beats ZFP in compression ratio at the same error bound.
- Competes closely with SZ3 on real sensor data (and sometimes even surpasses it in ratio).
- Is significantly better than lossless baselines whenever lossy compression is meaningful.

---

## EN: Building

### Windows (MSVC, DLL)

You need:

- MSVC (Visual Studio, `vcvars64.bat`)
- static or shared Zstandard and Zlib libraries (`zstd_static.lib`, `zlib_static.lib`)

Example build command (from repository root):

```bat
cmd /c "vcvars64.bat && cd /d D:\projekt\SzKompresor && cl /nologo /O2 /W4 /MD /LD /DSZY_BUILD_DLL ^
  src\szy_bytes.c src\szy_varuint.c src\szy_container_v8.c src\szy_tile_v8.c src\szy_buf.c src\szy_sha256.c ^
  src\szy_bitplane.c ^
  src\szy_hw.c ^
  src\szy_encode_v8.c src\szy_encode_f32.c src\szy_encode_u16.c ^
  src\szy_decode_v8.c src\szy_parallel.c src\szy_errors.c src\szy_exports.c ^
  /I include /I src ^
  /Fe:szytool.dll ^
  /link /LIBPATH:static zstd_static.lib zlib_static.lib"
```

This produces `szytool.dll` exposing the C API defined in `include/szy_dll.h`.

---

### Linux / POSIX (outline)

- Install Zstandard and Zlib development packages (`libzstd-dev`, `zlib1g-dev`, etc.).
- Compile all `src/*.c` into a shared library:

```bash
gcc -O3 -fPIC \
  src/szy_bytes.c src/szy_varuint.c src/szy_container_v8.c src/szy_tile_v8.c \
  src/szy_buf.c src/szy_sha256.c src/szy_bitplane.c src/szy_hw.c \
  src/szy_encode_v8.c src/szy_encode_f32.c src/szy_encode_u16.c \
  src/szy_decode_v8.c src/szy_parallel.c src/szy_errors.c src/szy_exports.c \
  -Iinclude -Isrc \
  -lzstd -lz \
  -shared -o libszy.so
```

(Remember to adjust paths/names to your actual layout.)

---

## EN: Basic C API usage

The core DLL API (see `include/szy_dll.h`) provides:

- `szy_compress_buffer_ex` – compress 2D array of `double`
- `szy_compress_f32_ex` – compress 2D array of `float32`
- `szy_compress_u16_ex` – compress 2D array of `uint16`
- `szy_decompress_into_buffer_ex2` – decode into user‑provided `double` buffer
- `szy_peek_shape_v8` – read `H/W/B` without full decode
- `szy_free_buffer` – free buffers returned by the DLL
- `szy_strerror` – translate error codes to human‑readable strings

### Example: compress / decompress (`double`)

```c
#include "szy_dll.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int H = 512, W = 512;
    double* data = malloc((size_t)H * W * sizeof(double));
    /* ... fill data ... */

    uint8_t* out_buf = NULL;
    size_t out_size = 0;
    double eps = 10.0;
    int block = 128;
    int num_workers = 0; /* 0 = auto */

    int rc = szy_compress_buffer_ex(
        data, H, W, eps, block,
        num_workers,
        &out_buf, &out_size
    );
    if (rc != SZY_OK) {
        fprintf(stderr, "compress error: %s\n", szy_strerror(rc));
        free(data);
        return 1;
    }

    printf("compressed to %zu bytes\n", out_size);

    /* decode into new buffer */
    double* dec = malloc((size_t)H * W * sizeof(double));
    int H2 = 0, W2 = 0;
    rc = szy_decompress_into_buffer_ex2(
        out_buf, out_size,
        dec, (size_t)H * W,
        num_workers,
        64, /* parallel_min_tiles */
        &H2, &W2
    );
    if (rc != SZY_OK) {
        fprintf(stderr, "decompress error: %s\n", szy_strerror(rc));
    } else {
        printf("decompressed shape: %dx%d\n", H2, W2);
    }

    szy_free_buffer(out_buf);
    free(data);
    free(dec);
    return 0;
}
```

---

## EN: Python usage (ctypes wrapper)

`bench_common.py` contains a simple `SzyDLL` wrapper around the DLL API.

Example:

```python
import numpy as np
from bench_common import SzyDLL

dll = SzyDLL("szytool.dll")  # or path to libszy.so via ctypes.CDLL

# Example: float32 array
H, W = 768, 1536
x = np.random.randn(H, W).astype(np.float32)

eps = 10.0
block = 128
num_workers = 0  # auto

blob = dll.compress(x, eps=eps, block=block, num_workers=num_workers)
print("Compressed size:", len(blob), "bytes")

out = np.empty((H, W), dtype=np.float64)
H2, W2 = dll.decompress_into_f64(
    blob, out,
    num_workers=num_workers,
    parallel_min_tiles=64
)
print("Decoded shape:", H2, W2)
```

`bench_public.py` builds on top of this wrapper to run full benchmark suites against SZ3/ZFP/Zstd/Blosc2 and save JSON/CSV/PNG reports.

---

## EN: Repository layout (typical)

```text
src/        - C sources (engine, container, tile, parallel, exports)
include/    - public C headers (szy_api.h, szy_dll.h, szy_rc.h, ...)
tests/      - benchmark scripts (bench_common.py, bench_public.py)
            - LaTeX and PDF documentation (whitepaper)
LICENSE     - AGPLv3
README.md   - this file
.gitignore  - ignores build artifacts and benchmark outputs
```

---

## EN: License and commercial use

The SZY engine is released under the **GNU Affero General Public License v3.0 (AGPL‑3.0)**.

- You are free to use, study, modify and redistribute the code under the terms of AGPLv3.
- If you integrate SZY into a program that you distribute or deploy as a network service, AGPLv3 requires you to make the corresponding source code available to your users.
- If you want to use SZY in a closed‑source or proprietary product without the obligations of AGPLv3 (for example, embedding the engine in commercial software, firmware, or SaaS without disclosing your code), **commercial licenses are available**.

For commercial licensing, OEM partnerships or support, please contact the author (see GitHub profile or repository contact information).

---

## PL: Przegląd

SZY jest kafelkowym silnikiem stratnej kompresji danych naukowych 2D z gwarancją błędu:

**Typy danych:**

- `float64`
- `float32`
- `uint16` (np. obrazy satelitarne, medyczne, CCD)

**Model błędu:**

- absolutny bound (`max |x - x̂| ≤ eps`)

**Format kontenera:**

- `SZ2D/v8` (magic `SZ2D`)
- integralność SHA‑256 (dane + kontener)
- indeks kafelków

Aktualne repozytorium opisuje moduł 2D (`SZ2D/v8`).  
Architektura jest przygotowana do rozszerzenia na 3D/4D w przyszłych wersjach.

---

## PL: Najważniejsze cechy

### Gwarantowany błąd

- użytkownik podaje `eps`,
- SZY dobiera kwantyzację, predykcję i eventy tak, by `max_err` był zbliżony do `eps`,
- w przypadku braku możliwości dotrzymania boundu przy danej konfiguracji encoder może zwrócić błąd `SZY_E_BOUNDS`.

### Architektura kafelkowa (B×B)

- niezależne tile 2D (np. `64×64`, `128×128`),
- łatwa równoległość,
- możliwość dekodowania fragmentów obrazu.

### Kontener z integralnością i indeksem

- SHA‑256 danych i kontenera,
- indeks długości tile (varuint) → szybkie obliczanie offsetów,
- równoległy decode na podstawie indeksu.

### Silnik (engine), nie tylko „goły kodek”

API DLL (`szy_dll.h`) z prostymi funkcjami:

- kompresja 2D (`double`, `float`, `uint16`),
- `decode-into` (bez dodatkowych alokacji),
- `peek shape`,
- obsługa błędów i wątków.

---

## PL: Wyniki na danych publicznych (skrót)

Na benchmarkach opisanych w repozytorium:

### Na obrazie typu Landsat (GeoTIFF sample)

Przy `eps=20`:

- SZY osiąga ok. **16×** kompresji,
- Zstd/Blosc2 (bezstratne) ok. **4.4×–4.5×**.

### Na danych sensorowych (UCI Energy, AirQuality)

Przy `eps=20`:

- **SZY:** `195–532×`,
- **SZ3:** `206–470×`,
- **ZFP:** `90–126×` (przy mniejszym błędzie niż `eps`),
- **Zstd:** `38–60×` (bezstratnie).

### Na syntetycznym polu klimatycznym

- SZY osiąga ponad **1000×** przy `eps=10–20`,
- SZ3 daje jeszcze wyższe ratio na tym super‑gładkim polu,
- ZFP i bezstratne kodeki są daleko w tyle.

Szczegółowe tabele i wykresy znajdują się w `compare_all.csv`, `report.md` i w katalogu `plots/` generowanym przez `bench_public.py`.

---

## PL: Budowanie i używanie

### Budowanie (Windows/MSVC)

Instrukcja kompilacji do DLL (`szytool.dll`) znajduje się w sekcji „Building” (EN).  
W skrócie: kompilujemy pliki `src/*.c` z nagłówkami z `include/`, linkujemy z Zstd/Zlib.

### Użycie w C

API DLL udostępnia funkcje:

- `szy_compress_buffer_ex` – kompresja `double**`,
- `szy_compress_f32_ex` – kompresja `float**`,
- `szy_compress_u16_ex` – kompresja `uint16**`,
- `szy_decompress_into_buffer_ex2` – dekodowanie bezpośrednio do bufora `double*`,
- `szy_peek_shape_v8` – odczyt `H/W/B` bez pełnego decode,
- `szy_free_buffer` – zwalnianie bufora zwracanego przez DLL.

Przykład użycia w C znajduje się w sekcji EN („Basic C API usage”).

### Użycie w Pythonie

`bench_common.py` zawiera klasę `SzyDLL`, która:

- ładuje DLL (Windows) lub `.so` (Linux),
- mapuje funkcje C na `ctypes`,
- udostępnia metodę `compress(x, eps, block, num_workers)` oraz `decompress_into_f64`.

Skrypt `bench_public.py` pokazuje kompletne wywołanie benchmarku na rzeczywistych danych.

---

## PL: Licencja i użycie komercyjne

Projekt SZY jest wydany na licencji **GNU Affero General Public License v3.0 (AGPL‑3.0)**.

- Możesz swobodnie używać, analizować, modyfikować i rozpowszechniać kod na zasadach AGPLv3.
- Jeżeli integrujesz SZY w aplikacji dystrybuowanej lub udostępnianej jako usługa sieciowa (SaaS, web API), AGPLv3 wymaga udostępnienia kodu źródłowego takiej aplikacji.
- Jeżeli chcesz używać SZY w zamkniętym, komercyjnym produkcie bez obowiązków AGPL (np. oprogramowanie, firmware, SaaS bez otwierania kodu), dostępne są **licencje komercyjne**.

W sprawie licencji komercyjnych, integracji OEM lub wsparcia technicznego skontaktuj się z autorem (dane kontaktowe w profilu GitHub lub w dokumentacji projektu).
