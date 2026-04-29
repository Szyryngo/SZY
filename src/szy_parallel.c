#include "szy_parallel.h"
#include "szy_tile_v8.h"
#include "szy_rc.h"

#include <stdlib.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static int add_overflow_size(size_t a, size_t b, size_t* out) {
  if (!out) return -1;
  if (a > SIZE_MAX - b) return -1;
  *out = a + b;
  return 0;
}

#ifdef _WIN32

typedef struct {
  const uint8_t* buf;
  size_t data_end;
  const size_t* offsets;

  int B, W, ntx, ntiles;
  double* img;

  volatile LONG next_tile;
  volatile LONG err;
} szy_win_tp_ctx_t;

static DWORD WINAPI szy_win_tp_worker(LPVOID arg) {
  szy_win_tp_ctx_t* c = (szy_win_tp_ctx_t*)arg;

  for (;;) {
    /* POPRAWKA #1: Sprawdź błąd PRZED pobraniem nowego tile */
    if (InterlockedCompareExchange(&c->err, 0, 0) != 0) return 0;

    LONG ti = InterlockedIncrement(&c->next_tile) - 1;

    /* POPRAWKA #1: Sprawdź błąd PO inkrementacji (inny wątek mógł ustawić) */
    if (InterlockedCompareExchange(&c->err, 0, 0) != 0) return 0;
    if (ti >= (LONG)c->ntiles) return 0;

    int ty = (int)ti / c->ntx;
    int tx = (int)ti % c->ntx;

    double* dst = c->img
      + (size_t)ty * (size_t)c->B * (size_t)c->W
      + (size_t)tx * (size_t)c->B;

    size_t pos = c->offsets[(int)ti];
    int rc = szy_tile_decode_into_v8(c->buf, c->data_end, &pos, c->B, dst, c->W);
    if (rc != SZY_OK) {
      InterlockedCompareExchange(&c->err, (LONG)((rc < 0) ? rc : SZY_E_TILE), 0);
      return 0;
    }
  }
}

#else  /* POSIX thread-pool */

typedef struct {
  const uint8_t* buf;
  size_t data_end;
  const size_t* offsets;

  int B, W, ntx, ntiles;
  double* img;

  int next_tile;
  int err;

  pthread_mutex_t mu;
} szy_tp_ctx_t;

static void* tp_worker(void* arg) {
  szy_tp_ctx_t* c = (szy_tp_ctx_t*)arg;

  for (;;) {
    int ti;

    /* POPRAWKA #1: Sprawdź błąd PRZED pobraniem nowego tile (atomowo z inkrementacją) */
    pthread_mutex_lock(&c->mu);
    if (c->err) { pthread_mutex_unlock(&c->mu); return NULL; }
    ti = c->next_tile++;
    pthread_mutex_unlock(&c->mu);

    if (ti >= c->ntiles) return NULL;

    int ty = ti / c->ntx;
    int tx = ti % c->ntx;

    double* dst = c->img
      + (size_t)ty * (size_t)c->B * (size_t)c->W
      + (size_t)tx * (size_t)c->B;

    size_t pos = c->offsets[ti];
    int rc = szy_tile_decode_into_v8(c->buf, c->data_end, &pos, c->B, dst, c->W);
    if (rc != SZY_OK) {
      pthread_mutex_lock(&c->mu);
      c->err = (rc < 0) ? rc : SZY_E_TILE;
      pthread_mutex_unlock(&c->mu);
      return NULL;
    }
  }
}

#endif

int szy_decode_parallel_tiles(
    const uint8_t* buf,
    const szy_container_hdr_t* hdr,
    double* img,
    int num_workers
) {
  if (!buf || !hdr || !img || num_workers <= 0) return SZY_EINVAL;
  if (!hdr->index_present || !hdr->tile_lengths) return SZY_EINVAL;

  int H = (int)hdr->H;
  int W = (int)hdr->W;
  int B = (int)hdr->block;
  if (H <= 0 || W <= 0 || B <= 0) return SZY_EINVAL;
  if ((H % B) || (W % B)) return SZY_E_BLOCK;

  int ntx = W / B;
  int nty = H / B;
  int ntiles = nty * ntx;
  if ((int)hdr->ntiles != ntiles) return SZY_E_TILECOUNT;

  size_t* offsets = (size_t*)malloc((size_t)ntiles * sizeof(size_t));
  if (!offsets) return SZY_E_OOM;

  offsets[0] = hdr->tiles_start;
  for (int i = 1; i < ntiles; i++) {
    size_t next;
    if (add_overflow_size(offsets[i - 1], (size_t)hdr->tile_lengths[i - 1], &next)) {
      free(offsets);
      return SZY_E_OVERFLOW;
    }
    offsets[i] = next;
  }

  {
    size_t end;
    if (add_overflow_size(offsets[ntiles - 1], (size_t)hdr->tile_lengths[ntiles - 1], &end)) {
      free(offsets);
      return SZY_E_OVERFLOW;
    }
    if (end > hdr->data_end) {
      free(offsets);
      return SZY_E_INDEX;
    }
  }

#ifdef _WIN32
  int nworkers = num_workers;
  if (nworkers > ntiles) nworkers = ntiles;
  if (nworkers < 1) nworkers = 1;

  HANDLE* threads = (HANDLE*)malloc((size_t)nworkers * sizeof(HANDLE));
  if (!threads) { free(offsets); return SZY_E_OOM; }

  szy_win_tp_ctx_t ctx;
  ctx.buf = buf;
  ctx.data_end = hdr->data_end;
  ctx.offsets = offsets;
  ctx.B = B;
  ctx.W = W;
  ctx.ntx = ntx;
  ctx.ntiles = ntiles;
  ctx.img = img;
  ctx.next_tile = 0;
  ctx.err = 0;

  int created = 0;
  for (int i = 0; i < nworkers; i++) {
    threads[i] = CreateThread(NULL, 0, szy_win_tp_worker, &ctx, 0, NULL);
    if (!threads[i]) {
      InterlockedCompareExchange(&ctx.err, (LONG)SZY_E_TILE, 0);
      break;
    }
    created++;
  }

  if (created > 0) {
    WaitForMultipleObjects((DWORD)created, threads, TRUE, INFINITE);
    for (int i = 0; i < created; i++) CloseHandle(threads[i]);
  }

  free(threads);
  free(offsets);

  return (ctx.err == 0) ? SZY_OK : (int)ctx.err;

#else
  int nworkers = num_workers;
  if (nworkers > ntiles) nworkers = ntiles;
  if (nworkers < 1) nworkers = 1;

  pthread_t* threads = (pthread_t*)malloc((size_t)nworkers * sizeof(pthread_t));
  if (!threads) { free(offsets); return SZY_E_OOM; }

  szy_tp_ctx_t ctx;
  ctx.buf = buf;
  ctx.data_end = hdr->data_end;
  ctx.offsets = offsets;
  ctx.B = B;
  ctx.W = W;
  ctx.ntx = ntx;
  ctx.ntiles = ntiles;
  ctx.img = img;
  ctx.next_tile = 0;
  ctx.err = SZY_OK;
  pthread_mutex_init(&ctx.mu, NULL);

  int created = 0;
  for (int i = 0; i < nworkers; i++) {
    if (pthread_create(&threads[i], NULL, tp_worker, &ctx) != 0) {
      pthread_mutex_lock(&ctx.mu);
      ctx.err = SZY_E_TILE;
      pthread_mutex_unlock(&ctx.mu);
      break;
    }
    created++;
  }

  for (int i = 0; i < created; i++) pthread_join(threads[i], NULL);

  pthread_mutex_destroy(&ctx.mu);
  free(threads);
  free(offsets);

  return (ctx.err == SZY_OK) ? SZY_OK : ctx.err;
#endif
}