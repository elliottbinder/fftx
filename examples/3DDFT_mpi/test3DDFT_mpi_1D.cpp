#include <mpi.h>
#include <complex>
#include <iostream>
#include <stdlib.h>     /* srand, rand */
#include "device_macros.h"

#include "fftx_mpi.hpp"

using namespace std;

#define DEBUG 0

inline int ceil_div(int a, int b) {
  return (a + b - 1) / b;
}

int main(int argc, char* argv[]) {
  if (argc != 9) {
    printf("usage: %s <M> <N> <K> <batch> <embedded> <forward> <complex> <check>\n", argv[0]);
    exit(-1);
  }

  MPI_Init(&argc, &argv);

  int rank;
  int p;

  MPI_Comm_size(MPI_COMM_WORLD, &p);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  // X dim is size M,
  // Y dim is size N,
  // Z dim is size K.
  // Sizes are given in real-space. i.e. M is size of input for R2C, output for C2R.
  // This allows the same plan to be used for both forward and inverse transforms.
  int M = atoi(argv[1]);
  int N = atoi(argv[2]);
  int K = atoi(argv[3]);

  int batch        = atoi(argv[4]);
  bool is_embedded = 0 < atoi(argv[5]);
  bool is_forward  = 0 < atoi(argv[6]);
  bool is_complex  = 0 < atoi(argv[7]);

  int check        = atoi(argv[8]);

  int trials = 1;

  // 1: basic test (first element),
  // 2: full test (local comparison of all elements)

  // (slowest to fastest)
  // R2C input is [K,       N, M]         doubles, block distributed Z.
  // C2R input is [N, M/2 + 1, K] complex doubles, block distributed X.
  // C2C input is [K,       N, M] complex doubles, block distributed Z.
  // TODO: check this
  // C2C inv   is [N,       M, K] complex doubles, block distributed Z.
  bool R2C = !is_complex &&  is_forward;
  bool C2R = !is_complex && !is_forward;
  bool C2C =  is_complex;
  int e = is_embedded ? 2 : 1;

  int Mi = C2R ? (M*e/2) + 1 : M*e;
  int Mi0 = ceil_div(Mi, p);
  int Mi1 = p;

  double *host_in, *dev_in;
  double *host_out, *dev_out;
  int CI = C2C || C2R ? 2 : 1; // complex input.
  int CO = C2C || R2C ? 2 : 1; // complex output.

  if (is_forward) {
    /*
    [(pz), ceil(K/pz), N, M]
    [(pz), ceil(K/pz), N, M*e] (embed)
    [(pz), Mo, ceil(K/pz), N] (stage 1, permute) (Mo depends on C2C or R2C, and embedded)
    [(pz), px, ceil(Mo/px), ceil(K/pz), N] (reshape)
    [(px), pz, ceil(Mo/px), ceil(K/pz), N] (a2a)
    [(px), ceil(Mo/px), pz, ceil(K/pz), N] (permute)
    [(px), ceil(Mo/px), pz*ceil(K/pz), N] (reshape)
    [(px), ceil(Mo/px), pz*ceil(K/pz), N*e] (embed)
    [(px), N*e, ceil(Mo/px), pz*ceil(K/pz)] (stage 2, permute)
    [(px), N*e, ceil(Mo/px), pz*ceil(K/pz)*e] (embed) --> TODO: embed could go into a smaller space?
    [(px), N*e, ceil(Mo/px), pz*ceil(K/pz)*e] (stage 3)
    [(px), N*e, ceil(Mo/px), K*e] (stage 3, if embedded in smaller space)
    */
    // TODO: what about when K % p != 0?

    int Ki0 = ceil_div(K, p);

    size_t in_size = sizeof(double) * Ki0 * N   * M*e * batch * CI;
    host_in  = (double *) malloc(in_size);
    int Mo = R2C ? (M*e/2+1) : M*e;
    int Mo0 = ceil_div(Mo, p);
    int Ko0 = Ki0; // embeds after collection from A2A.
    // TODO: check K here for rounding up.
    size_t out_size = sizeof(double) * N*e * Mo0 * p*Ko0*e * batch * CO;
    host_out = (double *) malloc(out_size);

    DEVICE_MALLOC(&dev_in , in_size);
    DEVICE_MALLOC(&dev_out, out_size);

    // () is distributed
    // assume layout is [(pz), Z/pz, Y, X, b] (slowest to fastest).
    // embed X in the middle of dimension of twice the size, pad with zeros.
    for (int l0 = 0; l0 < Ki0; l0++) {
      int l = rank * Ki0 + l0;
      for (int j = 0; j < N; j++) {
        for (int i = 0; i < M*e; i++) {
          for (int b = 0; b < batch; b++) {
            for (int c = 0; c < CI; c++) {
            host_in[((l0 * N*M*e + j * M*e + i)*batch + b)*CI + c] = (
              (K <= l) || (is_embedded && (i < M/2 || 3 * M/2 <= i))
            ) ?
              0.0 :
              1.0 * rand() / RAND_MAX;
            }
          }
        }
      }
    }

    DEVICE_MEM_COPY(dev_in, host_in, in_size, MEM_COPY_HOST_TO_DEVICE);
  } else { // is inverse
    /* Assumes inverse embedded keeps full doubled-embedded space.
    [(px), N*e, ceil(Mo/px), pz*ceil(K/pz)*e] (output of fwd)
    [(px), N*e, ceil(Mo/px), K*e] (output of fwd, if embedded puts into smaller space)
    [(px), N*e, ceil(Mo/px), K] (currently written) --> is actually this.
    NOTE: FOR NOW, assuming K is divisble by number of ranks.
    Looks like library code may be needed to changed to support otherwise.
    [(px), N*e, ceil(Mo/px), K*e] (stage 1)
    [(px), ceil(Mo/px), K*e, N*e] (stage 2, permute)
    [(px), ceil(Mo/px), pz, ceil(K*e/pz), N*e] (reshape)
    [(px), pz, ceil(Mo/px), ceil(K*e/pz), N*e] (permute)
    [(pz), px, ceil(Mo/px), ceil(K*e/pz), N*e] (a2a)
    [(pz), px*ceil(Mo/px), ceil(K*e/pz), N*e] (reshape)
    [(pz), ceil(K*e/pz), N*e, px*ceil(Mo/px)] (stage3, permute)
    [(pz), ceil(K*e/pz), N*e, Mo] (stage3, permute, embed?)
    */
    int Ki = K*e;
    size_t in_size = sizeof(double) * N*e * Mi0 * Ki * batch * CI;
    host_in  = (double *) malloc(in_size);
    DEVICE_MALLOC(&dev_in ,      in_size);

    int Mo = M*e;
    int Mo0 = ceil_div(Mo, p);
    int Ko = Ki;
    int Ko0 = ceil_div(Ko, p);
    size_t out_size = sizeof(double) * Ko0 * N*e * p*Mo0 * batch * CO;
    host_out = (double *) malloc(out_size);
    DEVICE_MALLOC(&dev_out,      out_size);

    // assume layout is [(px), Y, X'/px, Z] (slowest to fastest)
    for (int j = 0; j < N*e; j++) {
      for (int i0 = 0; i0 < Mi0; i0++) {
        int i = rank * Mi0 + i0;
        for (int l = 0; l < Ki; l++) {
          for (int b = 0; b < batch; b++) {
            for (int c = 0; c < CI; c++) {
              if (check == 1) {
                // simple check for inverse is all elements are equal to the first element.
                host_in[((j * Mi0*Ki + i0 * Ki + l)*batch + b) * CI + c] = (j == 0 && i == 0 && l == 0 && c == 0) ? 1.0 * M*e * N*e * K*e * (b + 1) : 0.0;
              } else {
                host_in[((j * Mi0*Ki + i0 * Ki + l)*batch + b) * CI + c] = 1.0 * rand() / RAND_MAX;
              }
            }
          }
        }
      }
    }
    DEVICE_MEM_COPY(dev_in, host_in, in_size, MEM_COPY_HOST_TO_DEVICE);
  } // end forward/inverse check.

  fftx_plan plan = fftx_plan_distributed_1d(p, M, N, K, batch, is_embedded, is_complex);
  for (int t = 0; t < trials; t++) {

    double start_time = MPI_Wtime();

    fftx_execute_1d(plan, (double*)dev_out, (double*)dev_in, (is_forward ? DEVICE_FFT_FORWARD : DEVICE_FFT_INVERSE));

    double end_time = MPI_Wtime();
    double max_time    = max_diff(start_time, end_time, MPI_COMM_WORLD);

    if (rank == 0) {
      cout
        << M << "," << N << "," << K << ","
        << batch << ","
        << p << ","
        << (is_embedded ? "embedded" : "") << ","
        << (is_forward ? "fwd" : "inv") << ","
        << (is_complex ? "complex" : "real") << ","
        << (check == 1 ? "first_elem" : "local") << ","
        << max_time;
      if (t < trials-1) { // only check last iter, will write its own end line.
        cout << endl;
      }
    }
  }

  MPI_Barrier(MPI_COMM_WORLD);

  if (check == 1) { // simple check, only check first element.
    bool correct = true;
    if (is_forward) {

      int Mo = R2C ? (M*e/2) + 1 : M*e;
      int Mo0 = ceil_div(Mo, p);

      int Ki0 = ceil_div(K, p);
      int Ko0 = Ki0; // embeds after collection from A2A.

      DEVICE_MEM_COPY(host_out, dev_out, sizeof(double) * N*e * Mo0 * p*Ko0*e * batch * CO, MEM_COPY_DEVICE_TO_HOST);

      double *first_elems = (double *) malloc(sizeof(double) * batch);
      for (int b = 0; b < batch; b++) {
        first_elems[b] = {};
      }

      // initial distribution is [ceil(Z/p), Y, X, b]
      for (int l0 = 0; l0 < Ki0; l0++) {
        int l = rank * Ki0 + l0;
        if (l < K) {
          for (int j = 0; j < N; j++) {
            for (int i = 0; i < M*e; i++) {
              for (int b = 0; b < batch; b++) {
                first_elems[b] += host_in[((l0 * N*M*e + j * M*e + i)*batch + b)*CI + 0];
                // skips imaginary elements.
              }
            }
          }
        }
      }

      for (int b = 0; b < batch; b++) {
        MPI_Allreduce(MPI_IN_PLACE, first_elems + b, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      }

      // distribution is [Y, X'/p, Z, b]
      if (rank == 0) {
        for (int b = 0; b < batch; b++) {
          if (abs(host_out[b*CI + 0] - first_elems[b]) > 1e-8) {
            correct = false;
          }
        }
      }
      free(first_elems);
    } else { // is inverse (C2C or C2R)
      // inv input
      // [(px), N*e, ceil(Mo/px), pz*ceil(K/pz)*e] (output of fwd)
      // [(px), N*e, ceil(Mo/px), K*e] (output of fwd, if embedded puts into smaller space)
      // inv output
      // [(pz), ceil(K*e/pz), N*e, Mo] (stage3, permute, embed?)

      int Ki = K*e;
      int Ki0 = ceil_div(Ki, p);
      int Ko0 = Ki0;

      int Mo0 = ceil_div(M*e, p);
      int Mo = p * Mo0;

      size_t out_size = sizeof(double) * Ko0 * N*e * Mo * batch * CO;
      DEVICE_MEM_COPY(host_out, dev_out, out_size, MEM_COPY_DEVICE_TO_HOST);

      for (int k0 = 0; k0 < Ko0; k0++) {
        int k = rank * Ko0 + k0;
        if (k < K*e) {
          for (int j = 0; j < N*e; j++) {
            for (int i = 0; i < M*e; i++) {
              for (int b = 0; b < batch; b++) {
                for (int c = 0; c < CO; c++) {
                  int tst_idx = ((k0 * N*e*Mo + j * Mo + i)*batch + b) * CO + c;

                  if (c == 0) {
                    if (abs(host_out[tst_idx] - 1.0 * M*e * N*e * K*e * (b+1)) > 1e-8) {
                      correct = false;
                    }
                  } else if (c == 1) {
                    if (abs(host_out[tst_idx] -                     0.0) > 1e-8) {
                      correct = false;
                    }
                  }
                }
              }
            }
          }
        }
      }
      MPI_Reduce(rank == 0 ? MPI_IN_PLACE : &correct, &correct, 1, MPI_C_BOOL, MPI_LAND, 0, MPI_COMM_WORLD);
    }
    if (rank == 0) {
      if (correct) {
        cout << ",1";
      } else {
        cout << ",0";
      }
    }
  } else if (check == 2) { // local 3D comparison, check all elements.
    // only check for N, M, K <= 32, and some small number of processors.
    if (M > 32 || N > 32 || K > 32 || p > 4) {
      if (rank == 0) {
        cout << ",X" << endl;
      }
      goto end;
    }
    double *href_in, *href_in_modified, *href_out, *htest_out;
    double *dref_in, *dref_out;
    int root = 0;
    int local_in_size, local_out_size;
    if (is_forward) { // fwd, initially Z distributed
      int Ki0 = ceil_div(K, p);
      int Mo = R2C ? (M*e/2 + 1) : M*e;
      int Mo0 = ceil_div(Mo, p);
      local_in_size  = Ki0 * N   * M*e * batch * CI;
      local_out_size = K*e * N*e * Mo0 * batch * CO;
    } else { // inv, initially X distributed
      int Kdim = ceil_div(K*e, p);
      local_in_size  = K*e  * N*e * Mi0 * batch * CI; // 9/20, TODO: is this right?
      local_out_size = Kdim * N*e * M*e * batch * CO;
    }
    // [Y, ceil(X'/px), Z] (no permutation on last stage)

    if (rank == root) {
      if (is_embedded) {
        // TODO: does it need to be any bigger?
        // any smaller?
        size_t global_in_size  = sizeof(double) * K*2 * N*2 * Mi  * batch * CI;
        size_t global_out_size = sizeof(double) * K*2 * N*2 * M*2 * batch * CO;

        href_in   = (double *) malloc(sizeof(double) * global_in_size );
        DEVICE_MALLOC(&dref_in ,      sizeof(double) * global_out_size);

        href_in_modified = (double *) malloc(global_in_size);
        href_out = (double *) malloc(global_out_size);
        htest_out = (double *) malloc(global_out_size);
        DEVICE_MALLOC(&dref_out, global_out_size);
      } else {
        href_in   = (double *) malloc(sizeof(double) * p * local_in_size);
        DEVICE_MALLOC(&dref_in ,      sizeof(double) * p * local_in_size);
        if (!is_forward) {
        href_in_modified = (double *) malloc(sizeof(double) * p * local_in_size);
        }
        href_out  = (double *) malloc(sizeof(double) * p * local_out_size);
        htest_out = (double *) malloc(sizeof(double) * p * local_out_size);
        DEVICE_MALLOC(&dref_out, sizeof(double) * p * local_out_size);
      }
    }
    // fwd [Z, Y, X, b] <= Gather pz on [(pz), Z/pz, Y, X, b]
    // inv [px, Y, X'/px, Z] <= Gather px on [(px), Y, X'/px, Z]
    int error = MPI_Gather(host_in, local_in_size, MPI_DOUBLE, href_in, local_in_size, MPI_DOUBLE, root, MPI_COMM_WORLD);
    // TODO update count for embedded.
    // fwd [px, Y, X'/px, Z] <= Gather px on [(px), Y, X'/px, Z]
    // inv [Z, Y, X, b] Gather pz on [(pz), Z/pz, Y, X, b]
    DEVICE_MEM_COPY(host_out, dev_out, sizeof(double) * local_out_size, MEM_COPY_DEVICE_TO_HOST);
    MPI_Gather(host_out, local_out_size, MPI_DOUBLE, htest_out, local_out_size, MPI_DOUBLE, root, MPI_COMM_WORLD);

    if (rank == root) {
      if (is_forward) {
        if (is_embedded) {
          // put gathered data into embedded tensor.
          // layout is [pz, ceil(K/pz), N, M*e]
          // gathered layout is [Z, Y, 2X, b], pad Y and Z dims.
          int Ki0 = ceil_div(K, p);
          int Ki1 = p;
          // pad front and back slabs.
          // embed doubles, padding halves.
          for (int k = 0; k < (K/2); k++) {
            for (int j = 0; j < N*2; j++) {
              for (int i = 0; i < Mi; i++) {
                for (int b = 0; b < batch; b++) {
                  for (int c = 0; c < CI; c++) {
                    href_in_modified[(((    0 + k) * N*2*Mi + j * Mi + i)*batch + b)*CI + c] = 0;
                    href_in_modified[(((3*K/2 + k) * N*2*Mi + j * Mi + i)*batch + b)*CI + c] = 0;
                  }
                }
              }
            }
          }
          for (int k1 = 0; k1 < Ki1; k1++) {
            for (int k0 = 0; k0 < Ki0; k0++) {
              int k_i = k1 * Ki0 + k0;
              int k_o = K/2 + k_i;
              for (int j = 0; j < N*2; j++) {
                int j_o = j;
                int j_i = j_o - N/2;
                for (int i = 0; i < Mi; i++) { // already embedded.
                  for (int b = 0; b < batch; b++) {
                    for (int c = 0; c < CI; c++) {
                      href_in_modified[
                        ((k_o * N*2*Mi + j_o * Mi + i)*batch + b)*CI + c
                      ] = (
                        (0 <= j_i && j_i < N) &&
                        // (0 <= i && i < Mi) &&
                        (0 <= k_i && k_i < K)
                      ) ? href_in[
                        ((k1 * Ki0*N*Mi + k0 * N*Mi + j_i * Mi + i) * batch + b) * CI + c
                      ] : 0.0;
                    }
                  }
                }
              }
            }
          }

          // swap pointers.
          double *tmp = href_in_modified;
          href_in_modified = href_in;
          href_in = tmp;
        } else {
          // fwd, not embedded.
        }
      } else { // is inverse
        if (is_embedded) {
          // permute already embedded [px, Ye, X'/px, Ze] input.
          // input should be [Ze, Ye, X']
          for (int i1 = 0; i1 < Mi1; i1++) {
            for (int j = 0; j < N*2; j++) {
              for (int i0 = 0; i0 < Mi0; i0++) {
                int i = i1 * Mi0 + i0;
                if (i < Mi) {
                  for (int k = 0; k < K*2; k++) {
                    for (int b = 0; b < batch; b++) {
                      for (int c = 0; c < CI; c++) {
                        href_in_modified[
                          ((k * N*2*Mi + j * Mi + i) * batch + b) * CI + c
                        ] = href_in[
                          ((i1 * N*2*Mi0*K*2 + j * Mi0*K*2 + i0 * K*2 + k) * batch + b) * CI + c
                        ];
                      }
                    }
                  }
                }
              }
            }
          }
          // swap pointers.
          double *tmp = href_in_modified;
          href_in_modified = href_in;
          href_in = tmp;

        } else { // inv not embedded
          // inverse input  layout is [px, Y, X'/px, Z]
          // inverse output layout is [Z, Y, X, b]
          // permute data to [Z, Y, X, b] from [px, Y, ceil(X'/px), Z] before calling cuFFT.
          for (int i1 = 0; i1 < Mi1; i1++) {
            for (int j = 0; j < N; j++) {
              for (int i0 = 0; i0 < Mi0; i0++) {
                int i = i1 * Mi0 + i0;
                if (i < Mi) {
                  for (int k = 0; k < K; k++) {
                    for (int b = 0; b < batch; b++) {
                      for (int c = 0; c < CI; c++) {
                      href_in_modified[
                        ((k * N*Mi + j * Mi + i) * batch + b) * CI + c
                      ] = href_in[
                        ((i1 * N*Mi0*K + j * Mi0*K + i0 * K + k) * batch + b) * CI + c
                      ];
                      }
                    }
                  }
                }
              }
            }
          }
          // swap pointers.
          double *tmp = href_in_modified;
          href_in_modified = href_in;
          href_in = tmp;
        } // end inv not embedded
      } // end if-else fwd/inv
      // if inverse, permute data before calling cuFFT.
      if (is_embedded) {
        // TODO: fix for other sizes?
        DEVICE_MEM_COPY(dref_in, href_in, sizeof(double) * K*2*N*2*Mi*batch*CI, MEM_COPY_HOST_TO_DEVICE);
      } else {
        DEVICE_MEM_COPY(dref_in, href_in, sizeof(double) * p * local_in_size, MEM_COPY_HOST_TO_DEVICE);
      }
      // create cuFFT plan 3d
      DEVICE_FFT_HANDLE plan;
      // slowest to fastest.
      if (C2C) {
        DEVICE_FFT_PLAN3D(&plan, K*e, N*e, M*e, DEVICE_FFT_Z2Z);
        DEVICE_FFT_EXECZ2Z(
          plan, (DEVICE_FFT_DOUBLECOMPLEX *) dref_in, (DEVICE_FFT_DOUBLECOMPLEX *) dref_out,
          is_forward ? DEVICE_FFT_FORWARD : DEVICE_FFT_INVERSE
        );
      } else if (C2R) {
        DEVICE_FFT_PLAN3D(&plan, K*e, N*e, M*e, DEVICE_FFT_Z2D);
        DEVICE_FFT_EXECZ2D(
          plan, (DEVICE_FFT_DOUBLECOMPLEX *) dref_in, (DEVICE_FFT_DOUBLEREAL *) dref_out
        );
      } else if (R2C) {
        DEVICE_FFT_PLAN3D(&plan, K*e, N*e, M*e, DEVICE_FFT_D2Z);
        DEVICE_FFT_EXECD2Z(
          plan, (DEVICE_FFT_DOUBLEREAL *) dref_in, (DEVICE_FFT_DOUBLECOMPLEX *) dref_out
        );
      } else {
        cout << "Error: unknown plan type." << endl;
        goto end;
      }

      {
        DEVICE_ERROR_T device_status = DEVICE_SYNCHRONIZE();
        if (device_status != DEVICE_SUCCESS) {
          fprintf(stderr, "DEVICE_SYNCHRONIZE returned error code %d after 3DFFT!\n", device_status);
        }
      }


      if (is_embedded) {
        // TODO: update for Mo R2C. and others.
        DEVICE_MEM_COPY(href_out, dref_out, sizeof(double) * K*2*N*2*M*2*batch*CO, MEM_COPY_DEVICE_TO_HOST);
      } else {
        DEVICE_MEM_COPY(href_out, dref_out, sizeof(double) * p * local_out_size, MEM_COPY_DEVICE_TO_HOST);
      }

      {
        DEVICE_ERROR_T device_status = DEVICE_SYNCHRONIZE();
        if (device_status != DEVICE_SUCCESS) {
          fprintf(stderr, "DEVICE_SYNCHRONIZE returned error code %d after 3DFFT!\n", device_status);
        }
      }

      // TODO: fix for embedded.
      int m = R2C ? (M*e/2) + 1 : M*e;
      int m0 = ceil_div(m, p);
      int m1 = p;

      // check href_out against htest_out.
      bool correct = true;
      if (is_forward) {
        for (int i1 = 0; i1 < m1; i1++) {
          for (int i0 = 0; i0 < m0; i0++) {
            int i = i1 * m0 + i0;
            if (i < m) {
              for (int j = 0; j < N*e; j++) {
                for (int k = 0; k < K*e; k++) {
                  for (int b = 0; b < batch; b++) {
                      int test_idx2 = ((i1 * N*e*m0*K*e + j * m0*K*e + i0 * K*e + k)*batch + b) * CO;
                      int ref_idx2  = ((k  * N*e*m      + j * m      +            i)*batch + b) * CO;
                      if (DEBUG) {
                        bool same = abs(href_out[ref_idx2] - htest_out[test_idx2]) < 1e-8;
                        same     &= abs(href_out[ref_idx2+1] - htest_out[test_idx2+1]) < 1e-8;
                        cout << "(" << k << "," << j << "," << i << ")\t";
                        printf(
                          "%12f %12f\t%12f %12f\t%s\n",
                          href_out [ ref_idx2 + 0], href_out [ ref_idx2 + 1],
                          htest_out[test_idx2 + 0], htest_out[test_idx2 + 1],
                          (same ? "" : "X")
                        );
                      }

                    for (int c = 0; c < CO; c++) {
                      int tst_idx = ((i1 * N*e*m0*K*e + j * m0*K*e + i0 * K*e + k)*batch + b) * CO + c;
                      int ref_idx = ((k  * N*e*m      + j * m      +            i)*batch + b) * CO + c;

                      if (abs(href_out[ref_idx] - htest_out[tst_idx]) > 1e-8) {
                        correct = false;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      } else { // inverse
        for (int k = 0; k < K*e; k++) {
          for (int j = 0; j < N*e; j++) {
            for (int i = 0; i < M*e; i++) {
              for (int b = 0; b < batch; b++) {

                if (DEBUG) {
                  int ref_idx2 = ((k * N*e*M*e + j * M*e + i)*batch + b) * CO;
                  int tst_idx2 = ((k * N*e*M*e + j * M*e + i)*batch + b) * CO;
                  printf("%f\t%f\n", href_out[ref_idx2], htest_out[tst_idx2]);
                }
                for (int c = 0; c < CO; c++) {
                  int ref_idx = ((k * N*e*M*e + j * M*e + i)*batch + b) * CO + c;
                  int tst_idx = ((k * N*e*M*e + j * M*e + i)*batch + b) * CO + c;
                  if (abs(href_out[ref_idx] - htest_out[tst_idx]) > 1e-8) {
                    correct = false;
                  }
                }
              }
            }
          }
        }
      }
      if (correct) {
        cout << ",1";
      } else {
        cout << ",0";
      }

      free(href_in);
      free(href_out);
      free(htest_out);
      DEVICE_FREE(dref_in);
      DEVICE_FREE(dref_out);
    } // end root check.
  } else { // end check on correctness check
    // not checking.
    if (rank == 0) {
      cout << ",-";
    }
  }

  if (rank == 0) {
    cout << endl;
  }

end:
  fftx_plan_destroy(plan);

  DEVICE_FREE(dev_in);
  DEVICE_FREE(dev_out);

  free(host_in);
  free(host_out);

  MPI_Finalize();

  return 0;
}
