/*
 * test_superlu_dist_gpu.c
 *
 * SuperLU_DIST 9.2.1 GPU correctness test.
 *
 * Matrix:
 *   2-D five-point stencil, A(i,i)=5 and neighbour entries=-1.
 * Exact solution:
 *   x_true = [1,1,...,1]^T.
 * Storage:
 *   Contiguous distributed rows in SuperLU_DIST NR_loc format.
 *
 * Usage:
 *   mpirun -np 1 ./main
 *   mpirun -np 1 ./main 256
 *   mpirun -np 2 ./main 256 1 2
 *
 * Arguments:
 *   side [nprow npcol]
 *
 * For one V100, start with one MPI rank.
 */

#include <mpi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "superlu_ddefs.h"

#define TEST_TOL 1.0e-10

typedef struct {
    int_t n;
    int_t m_loc;
    int_t fst_row;
    int_t nnz_loc;
    int_t *rowptr;
    int_t *colind;
    double *nzval;
    double *rhs;
    double *x;
} LocalProblem;

static void fail(MPI_Comm comm, int rank, const char *message)
{
    if (rank == 0) {
        fprintf(stderr, "Error: %s\n", message);
        fflush(stderr);
    }
    MPI_Abort(comm, EXIT_FAILURE);
}

static int parse_positive_int(const char *text, const char *name,
                              MPI_Comm comm, int rank)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);

    if (text == end || *end != '\0' || value <= 0 || value > 30000) {
        if (rank == 0)
            fprintf(stderr, "Error: invalid %s: %s\n", name, text);
        MPI_Abort(comm, EXIT_FAILURE);
    }
    return (int)value;
}

static void row_partition(int_t n, int rank, int nprocs,
                          int_t *m_loc, int_t *fst_row)
{
    const int_t base = n / nprocs;
    const int_t rem = n % nprocs;

    *m_loc = base + (rank < rem);
    *fst_row = (int_t)rank * base + (rank < rem ? rank : rem);
}

static int degree_of_row(int_t global_row, int side)
{
    const int_t r = global_row / side;
    const int_t c = global_row % side;
    int degree = 0;

    if (r > 0) ++degree;
    if (c > 0) ++degree;
    if (c + 1 < side) ++degree;
    if (r + 1 < side) ++degree;

    return degree;
}

/*
 * Build local CSR/NR_loc rows.
 *
 * rowptr, colind and nzval are allocated with SuperLU_DIST allocators
 * because Destroy_CompRowLoc_Matrix_dist() will free them.
 */
static void build_problem(int side, int rank, int nprocs, LocalProblem *p)
{
    p->n = (int_t)side * side;
    row_partition(p->n, rank, nprocs, &p->m_loc, &p->fst_row);

    if (p->m_loc <= 0)
        fail(MPI_COMM_WORLD, rank,
             "matrix order must be at least the MPI rank count");

    p->nnz_loc = 0;
    for (int_t i = 0; i < p->m_loc; ++i)
        p->nnz_loc += degree_of_row(p->fst_row + i, side) + 1;

    p->rowptr = intMalloc_dist(p->m_loc + 1);
    p->colind = intMalloc_dist(p->nnz_loc);
    p->nzval  = doubleMalloc_dist(p->nnz_loc);
    p->rhs    = doubleMalloc_dist(p->m_loc);
    p->x      = doubleMalloc_dist(p->m_loc);

    if (!p->rowptr || !p->colind || !p->nzval || !p->rhs || !p->x)
        fail(MPI_COMM_WORLD, rank, "host allocation failed");

    int_t pos = 0;
    p->rowptr[0] = 0;

    for (int_t i = 0; i < p->m_loc; ++i) {
        const int_t g = p->fst_row + i;
        const int_t r = g / side;
        const int_t c = g % side;
        double row_sum = 0.0;

#define INSERT_ENTRY(column, value)          \
        do {                                 \
            p->colind[pos] = (column);       \
            p->nzval[pos] = (value);         \
            row_sum += (value);              \
            ++pos;                           \
        } while (0)

        if (r > 0)          INSERT_ENTRY(g - side, -1.0);
        if (c > 0)          INSERT_ENTRY(g - 1,    -1.0);
                            INSERT_ENTRY(g,         5.0);
        if (c + 1 < side)   INSERT_ENTRY(g + 1,    -1.0);
        if (r + 1 < side)   INSERT_ENTRY(g + side, -1.0);

#undef INSERT_ENTRY

        p->rowptr[i + 1] = pos;

        /* b=A*1, and pdgssvx overwrites x with the solution. */
        p->rhs[i] = row_sum;
        p->x[i] = row_sum;
    }

    if (pos != p->nnz_loc)
        fail(MPI_COMM_WORLD, rank, "internal nnz count mismatch");
}

static void make_gather_layout(int_t n, int nprocs,
                               int **counts, int **displs)
{
    *counts = (int *)malloc((size_t)nprocs * sizeof(int));
    *displs = (int *)malloc((size_t)nprocs * sizeof(int));

    if (!*counts || !*displs)
        fail(MPI_COMM_WORLD, 0, "Allgatherv layout allocation failed");

    for (int r = 0; r < nprocs; ++r) {
        int_t m_loc, fst_row;
        row_partition(n, r, nprocs, &m_loc, &fst_row);
        (*counts)[r] = (int)m_loc;
        (*displs)[r] = (int)fst_row;
    }
}

static double relative_solution_error(const LocalProblem *p, MPI_Comm comm)
{
    long double local_err2 = 0.0L;
    long double local_ref2 = (long double)p->m_loc;

    for (int_t i = 0; i < p->m_loc; ++i) {
        const long double d = (long double)p->x[i] - 1.0L;
        local_err2 += d * d;
    }

    long double global_err2 = 0.0L, global_ref2 = 0.0L;
    MPI_Allreduce(&local_err2, &global_err2, 1,
                  MPI_LONG_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_ref2, &global_ref2, 1,
                  MPI_LONG_DOUBLE, MPI_SUM, comm);

    return sqrt((double)(global_err2 / global_ref2));
}

static double relative_residual(int side, const LocalProblem *p,
                                const double *x_global, MPI_Comm comm)
{
    long double local_r2 = 0.0L, local_b2 = 0.0L;

    for (int_t i = 0; i < p->m_loc; ++i) {
        const int_t g = p->fst_row + i;
        const int_t r = g / side;
        const int_t c = g % side;

        long double ax = 5.0L * x_global[g];
        if (r > 0)        ax -= x_global[g - side];
        if (c > 0)        ax -= x_global[g - 1];
        if (c + 1 < side) ax -= x_global[g + 1];
        if (r + 1 < side) ax -= x_global[g + side];

        const long double b = p->rhs[i];
        const long double res = b - ax;
        local_r2 += res * res;
        local_b2 += b * b;
    }

    long double global_r2 = 0.0L, global_b2 = 0.0L;
    MPI_Allreduce(&local_r2, &global_r2, 1,
                  MPI_LONG_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_b2, &global_b2, 1,
                  MPI_LONG_DOUBLE, MPI_SUM, comm);

    return global_b2 > 0.0L
               ? sqrt((double)(global_r2 / global_b2))
               : (global_r2 == 0.0L ? 0.0 : INFINITY);
}

int main(int argc, char **argv)
{
    int world_rank, world_size, thread_level;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &thread_level);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    int side = 256;
    if (argc >= 2)
        side = parse_positive_int(argv[1], "side",
                                  MPI_COMM_WORLD, world_rank);

    int dims[2] = {0, 0};

    if (argc == 4) {
        dims[0] = parse_positive_int(argv[2], "nprow",
                                     MPI_COMM_WORLD, world_rank);
        dims[1] = parse_positive_int(argv[3], "npcol",
                                     MPI_COMM_WORLD, world_rank);
    } else if (argc == 3 || argc > 4) {
        fail(MPI_COMM_WORLD, world_rank,
             "usage: ./main [side [nprow npcol]]");
    } else {
        MPI_Dims_create(world_size, 2, dims);
    }

    if (dims[0] * dims[1] != world_size)
        fail(MPI_COMM_WORLD, world_rank,
             "nprow*npcol must equal the MPI rank count");

    superlu_dist_options_t options;
    set_default_options_dist(&options);
    options.superlu_acc_offload = 1;
    options.IterRefine = SLU_DOUBLE;
    options.PrintStat = YES;

    int offload_enabled = 0;
    int gpu_count = 0;
    int selected_gpu = -1;

#ifdef GPU_ACC
    offload_enabled = get_acc_offload(&options);

    if (offload_enabled) {
        MPI_Comm local_comm;
        int local_rank;

        MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED,
                            world_rank, MPI_INFO_NULL, &local_comm);
        MPI_Comm_rank(local_comm, &local_rank);

        gpuGetDeviceCount(&gpu_count);
        if (gpu_count <= 0)
            fail(MPI_COMM_WORLD, world_rank,
                 "GPU-enabled SuperLU_DIST found no visible GPU");

        selected_gpu = local_rank % gpu_count;
        gpuSetDevice(selected_gpu);
        gpuFree(0); /* initialize CUDA before timing */

        MPI_Comm_free(&local_comm);
    }
#endif

    gridinfo_t grid;
    superlu_gridinit(MPI_COMM_WORLD, dims[0], dims[1], &grid);

    if (grid.iam < 0) {
        superlu_gridexit(&grid);
        MPI_Finalize();
        return EXIT_SUCCESS;
    }

    LocalProblem problem;
    memset(&problem, 0, sizeof(problem));
    build_problem(side, grid.iam, world_size, &problem);

    SuperMatrix A;
    dCreate_CompRowLoc_Matrix_dist(
        &A,
        problem.n,
        problem.n,
        problem.nnz_loc,
        problem.m_loc,
        problem.fst_row,
        problem.nzval,
        problem.colind,
        problem.rowptr,
        SLU_NR_loc,
        SLU_D,
        SLU_GE);

    dScalePermstruct_t scale_perm;
    dLUstruct_t lu;
    dSOLVEstruct_t solve;
    SuperLUStat_t stat;

    dScalePermstructInit(problem.n, problem.n, &scale_perm);
    dLUstructInit(problem.n, &lu);
    PStatInit(&stat);

    double *berr = doubleMalloc_dist(1);
    if (!berr)
        fail(MPI_COMM_WORLD, world_rank,
             "backward-error allocation failed");

    int major, minor, patch;
    superlu_dist_GetVersionNumber(&major, &minor, &patch);

    if (grid.iam == 0) {
        const int_t global_nnz =
            (int_t)5 * problem.n - (int_t)4 * side;

        printf("========================================\n");
        printf("SuperLU_DIST GPU smoke test\n");
        printf("library version       : %d.%d.%d\n",
               major, minor, patch);
        printf("MPI ranks             : %d\n", world_size);
        printf("MPI thread level      : %d\n", thread_level);
        printf("process grid          : %d x %d\n", dims[0], dims[1]);
        printf("grid side             : %d\n", side);
        printf("matrix order          : %lld\n",
               (long long)problem.n);
        printf("global nnz            : %lld\n",
               (long long)global_nnz);
        printf("storage               : distributed NR_loc\n");
#ifdef GPU_ACC
        printf("GPU-enabled build     : yes\n");
        printf("accelerator offload   : %s\n",
               offload_enabled ? "enabled" : "disabled");
        printf("visible GPUs          : %d\n", gpu_count);
#else
        printf("GPU-enabled build     : no\n");
        printf("accelerator offload   : unavailable\n");
#endif
        printf("========================================\n");
        print_sp_ienv_dist(&options);
        fflush(stdout);
    }

#ifdef GPU_ACC
    if (offload_enabled) {
        printf("Rank %d uses GPU %d\n", grid.iam, selected_gpu);
        fflush(stdout);
    }
#endif

    MPI_Barrier(grid.comm);
    const double begin = MPI_Wtime();

    int info = 0;
    pdgssvx(
        &options,
        &A,
        &scale_perm,
        problem.x,
        problem.m_loc,
        1,
        &grid,
        &lu,
        &solve,
        berr,
        &stat,
        &info);

    MPI_Barrier(grid.comm);
    const double local_total = MPI_Wtime() - begin;

    const double local_factor = (double)stat.utime[FACT];
    const double local_solve = (double)stat.utime[SOLVE];
    double total_time = 0.0, factor_time = 0.0, solve_time = 0.0;

    MPI_Reduce(&local_total, &total_time, 1, MPI_DOUBLE,
               MPI_MAX, 0, grid.comm);
    MPI_Reduce(&local_factor, &factor_time, 1, MPI_DOUBLE,
               MPI_MAX, 0, grid.comm);
    MPI_Reduce(&local_solve, &solve_time, 1, MPI_DOUBLE,
               MPI_MAX, 0, grid.comm);

    /* PStatPrint is collective. */
    PStatPrint(&options, &stat, &grid);

    int *counts = NULL, *displs = NULL;
    make_gather_layout(problem.n, world_size, &counts, &displs);

    double *x_global = doubleMalloc_dist(problem.n);
    if (!x_global)
        fail(MPI_COMM_WORLD, world_rank,
             "global-solution allocation failed");

    MPI_Allgatherv(
        problem.x,
        (int)problem.m_loc,
        MPI_DOUBLE,
        x_global,
        counts,
        displs,
        MPI_DOUBLE,
        grid.comm);

    const double rel_res =
        relative_residual(side, &problem, x_global, grid.comm);
    const double rel_xerr =
        relative_solution_error(&problem, grid.comm);

    int failed =
        info != 0 ||
        !isfinite(rel_res) ||
        !isfinite(rel_xerr) ||
        rel_res > TEST_TOL ||
        rel_xerr > TEST_TOL;

#ifdef GPU_ACC
    if (!offload_enabled)
        failed = 1;
#else
    failed = 1;
#endif

    int global_failed = 0;
    MPI_Allreduce(&failed, &global_failed, 1,
                  MPI_INT, MPI_MAX, grid.comm);

    if (grid.iam == 0) {
        printf("\nResults\n");
        printf("----------------------------------------\n");
        printf("pdgssvx info          : %d\n", info);
        printf("total wall time       : %.6f s\n", total_time);
        printf("factorization time    : %.6f s\n", factor_time);
        printf("solve time            : %.6f s\n", solve_time);
        printf("reported backward err : %.12e\n", berr[0]);
        printf("relative residual     : %.12e\n", rel_res);
        printf("relative x error      : %.12e\n", rel_xerr);
        printf("x[0]                  : %.12e\n", x_global[0]);
        printf("x[n/2]                : %.12e\n",
               x_global[problem.n / 2]);
        printf("x[n-1]                : %.12e\n",
               x_global[problem.n - 1]);
        printf("TEST                  : %s\n",
               global_failed ? "FAILED" : "PASSED");
    }

    free(counts);
    free(displs);
    SUPERLU_FREE(x_global);

    Destroy_CompRowLoc_Matrix_dist(&A);
    dScalePermstructFree(&scale_perm);
    dDestroy_LU(problem.n, &grid, &lu);
    dLUstructFree(&lu);

    if (options.SolveInitialized)
        dSolveFinalize(&options, &solve);

    PStatFree(&stat);
    SUPERLU_FREE(problem.rhs);
    SUPERLU_FREE(problem.x);
    SUPERLU_FREE(berr);

    superlu_gridexit(&grid);
    MPI_Finalize();

    return global_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
