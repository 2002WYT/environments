#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include "StrumpackSparseSolverMPIDist.hpp"

using strumpack::CompressionType;
using strumpack::KrylovSolver;
using strumpack::MatchingJob;
using strumpack::ReorderingStrategy;
using strumpack::ReturnCode;
using strumpack::StrumpackSparseSolverMPIDist;

namespace {

double exact_value(int global_index) {
    // Deterministic non-constant exact solution.
    return 1.0 + 0.25 * std::sin(0.01 * global_index)
               + 0.10 * std::cos(0.013 * global_index);
}

std::vector<int> make_block_distribution(int global_rows, int mpi_size) {
    std::vector<int> dist(mpi_size + 1, 0);
    const int base = global_rows / mpi_size;
    const int remainder = global_rows % mpi_size;

    for (int p = 0; p < mpi_size; ++p) {
        dist[p + 1] = dist[p] + base + (p < remainder ? 1 : 0);
    }
    return dist;
}

bool all_ranks_success(bool local_success, MPI_Comm comm) {
    const int local = local_success ? 1 : 0;
    int global = 0;
    MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_MIN, comm);
    return global == 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    
    int rank = 0;
    int provided = MPI_THREAD_SINGLE;

    int mpi_status = MPI_Init_thread(
        &argc,
        &argv,
        MPI_THREAD_MULTIPLE,
        &provided
    );
    int mpi_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

    if (mpi_status != MPI_SUCCESS) {
        std::cerr << "Error: MPI_Init_thread failed." << std::endl;
        return 1;
    }

    if (provided < MPI_THREAD_MULTIPLE) {
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);

        if (rank == 0) {
            std::cerr
                << "Error: SLATE requires MPI_THREAD_MULTIPLE, "
                << "but MPI provided thread level "
                << provided << "." << std::endl;
        }

        MPI_Finalize();
        return 1;
    }

    // argv[1] is the number of grid points in each direction.
    // Global matrix dimension is grid_n * grid_n.
    int grid_n = 100;
    const bool has_grid_argument = argc >= 2 && argv[1][0] != '-';
    if (has_grid_argument) {
        grid_n = std::atoi(argv[1]);
    }

    // Pass only STRUMPACK options to its parser. This removes our optional
    // positional grid_n argument while retaining argv[0] as the program name.
    std::vector<char*> strumpack_argv;
    strumpack_argv.reserve(static_cast<std::size_t>(argc));
    strumpack_argv.push_back(argv[0]);
    for (int i = has_grid_argument ? 2 : 1; i < argc; ++i) {
        strumpack_argv.push_back(argv[i]);
    }
    const int strumpack_argc = static_cast<int>(strumpack_argv.size());

    if (grid_n < 2) {
        if (rank == 0) {
            std::cerr << "Error: grid_n must be at least 2.\n";
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    const long long global_rows_ll = 1LL * grid_n * grid_n;
    if (global_rows_ll > static_cast<long long>(std::numeric_limits<int>::max())) {
        if (rank == 0) {
            std::cerr << "Error: matrix dimension exceeds 32-bit integer range.\n";
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    const int global_rows = static_cast<int>(global_rows_ll);
    if (global_rows < mpi_size) {
        if (rank == 0) {
            std::cerr << "Error: matrix has fewer rows than MPI processes.\n";
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    const std::vector<int> dist = make_block_distribution(global_rows, mpi_size);
    const int first_global_row = dist[rank];
    const int local_rows = dist[rank + 1] - dist[rank];

    std::vector<int> row_ptr(local_rows + 1, 0);
    std::vector<int> col_ind;
    std::vector<double> values;
    col_ind.reserve(static_cast<std::size_t>(5) * local_rows);
    values.reserve(static_cast<std::size_t>(5) * local_rows);

    std::vector<double> b(local_rows, 0.0);
    std::vector<double> x(local_rows, 0.0);
    std::vector<double> x_exact(local_rows, 0.0);

    // Build a block-row distributed 2D five-point Poisson matrix.
    // The local CSR column indices are global column indices.
    for (int local_row = 0; local_row < local_rows; ++local_row) {
        const int global_row = first_global_row + local_row;
        const int row = global_row / grid_n;
        const int col = global_row % grid_n;

        double rhs = 0.0;

        if (row > 0) {
            const int j = global_row - grid_n;
            col_ind.push_back(j);
            values.push_back(-1.0);
            rhs -= exact_value(j);
        }
        if (col > 0) {
            const int j = global_row - 1;
            col_ind.push_back(j);
            values.push_back(-1.0);
            rhs -= exact_value(j);
        }

        col_ind.push_back(global_row);
        values.push_back(4.0);
        rhs += 4.0 * exact_value(global_row);

        if (col + 1 < grid_n) {
            const int j = global_row + 1;
            col_ind.push_back(j);
            values.push_back(-1.0);
            rhs -= exact_value(j);
        }
        if (row + 1 < grid_n) {
            const int j = global_row + grid_n;
            col_ind.push_back(j);
            values.push_back(-1.0);
            rhs -= exact_value(j);
        }

        row_ptr[local_row + 1] = static_cast<int>(col_ind.size());
        b[local_row] = rhs;
        x_exact[local_row] = exact_value(global_row);
    }

    long long local_nnz = static_cast<long long>(col_ind.size());
    long long global_nnz = 0;
    MPI_Reduce(&local_nnz, &global_nnz, 1, MPI_LONG_LONG_INT,
               MPI_SUM, 0, MPI_COMM_WORLD);

    int exit_code = EXIT_SUCCESS;

    {
        StrumpackSparseSolverMPIDist<double, int> solver(MPI_COMM_WORLD, true);

        // Safe defaults for an installation-verification test.
        // Command-line STRUMPACK options appearing later may override them.
        solver.options().set_matching(MatchingJob::NONE);
        solver.options().set_compression(CompressionType::NONE);
        solver.options().set_Krylov_solver(KrylovSolver::DIRECT);
        solver.options().set_reordering_method(ReorderingStrategy::METIS);
        solver.options().set_from_command_line(strumpack_argc, strumpack_argv.data());

        solver.set_distributed_csr_matrix(
            local_rows,
            row_ptr.data(),
            col_ind.data(),
            values.data(),
            dist.data(),
            true  // symmetric sparsity pattern
        );

        if (rank == 0) {
            std::cout << "========================================\n"
                      << "STRUMPACK MPI distributed validation\n"
                      << "2D Poisson grid : " << grid_n << " x " << grid_n << '\n'
                      << "Matrix size     : " << global_rows << " x " << global_rows << '\n'
                      << "Nonzeros        : " << global_nnz << '\n'
                      << "MPI processes   : " << mpi_size << '\n'
                      << "========================================\n";
        }

        MPI_Barrier(MPI_COMM_WORLD);
        const double t0 = MPI_Wtime();
        const ReturnCode reorder_status = solver.reorder(grid_n, grid_n);
        MPI_Barrier(MPI_COMM_WORLD);
        const double t1 = MPI_Wtime();

        bool ok = all_ranks_success(reorder_status == ReturnCode::SUCCESS,
                                    MPI_COMM_WORLD);
        if (!ok) {
            if (rank == 0) {
                std::cerr << "STRUMPACK reorder failed.\n";
            }
            exit_code = EXIT_FAILURE;
        }

        ReturnCode factor_status = reorder_status;
        ReturnCode solve_status = reorder_status;
        double t2 = t1;
        double t3 = t1;

        if (ok) {
            factor_status = solver.factor();
            MPI_Barrier(MPI_COMM_WORLD);
            t2 = MPI_Wtime();
            ok = all_ranks_success(factor_status == ReturnCode::SUCCESS,
                                   MPI_COMM_WORLD);
            if (!ok) {
                if (rank == 0) {
                    std::cerr << "STRUMPACK factorization failed.\n";
                }
                exit_code = EXIT_FAILURE;
            }
        }

        if (ok) {
            solve_status = solver.solve(b.data(), x.data());
            MPI_Barrier(MPI_COMM_WORLD);
            t3 = MPI_Wtime();
            ok = all_ranks_success(solve_status == ReturnCode::SUCCESS,
                                   MPI_COMM_WORLD);
            if (!ok) {
                if (rank == 0) {
                    std::cerr << "STRUMPACK solve failed.\n";
                }
                exit_code = EXIT_FAILURE;
            }
        }

        double reorder_time = t1 - t0;
        double factor_time = t2 - t1;
        double solve_time = t3 - t2;
        double max_reorder_time = 0.0;
        double max_factor_time = 0.0;
        double max_solve_time = 0.0;
        MPI_Reduce(&reorder_time, &max_reorder_time, 1, MPI_DOUBLE,
                   MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&factor_time, &max_factor_time, 1, MPI_DOUBLE,
                   MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&solve_time, &max_solve_time, 1, MPI_DOUBLE,
                   MPI_MAX, 0, MPI_COMM_WORLD);

        if (ok) {
            // Gather x only for independent residual verification.
            std::vector<int> recv_counts(mpi_size, 0);
            for (int p = 0; p < mpi_size; ++p) {
                recv_counts[p] = dist[p + 1] - dist[p];
            }

            std::vector<double> x_global(global_rows, 0.0);
            MPI_Allgatherv(x.data(), local_rows, MPI_DOUBLE,
                           x_global.data(), recv_counts.data(), dist.data(),
                           MPI_DOUBLE, MPI_COMM_WORLD);

            double local_residual_sq = 0.0;
            double local_rhs_sq = 0.0;
            double local_error_sq = 0.0;
            double local_exact_sq = 0.0;
            double local_max_error = 0.0;

            for (int local_row = 0; local_row < local_rows; ++local_row) {
                const int global_row = first_global_row + local_row;

                double ax = 0.0;
                for (int jj = row_ptr[local_row]; jj < row_ptr[local_row + 1]; ++jj) {
                    ax += values[jj] * x_global[col_ind[jj]];
                }

                const double residual = b[local_row] - ax;
                const double error = x[local_row] - x_exact[local_row];

                local_residual_sq += residual * residual;
                local_rhs_sq += b[local_row] * b[local_row];
                local_error_sq += error * error;
                local_exact_sq += x_exact[local_row] * x_exact[local_row];
                local_max_error = std::max(local_max_error, std::abs(error));
            }

            double global_residual_sq = 0.0;
            double global_rhs_sq = 0.0;
            double global_error_sq = 0.0;
            double global_exact_sq = 0.0;
            double global_max_error = 0.0;

            MPI_Reduce(&local_residual_sq, &global_residual_sq, 1, MPI_DOUBLE,
                       MPI_SUM, 0, MPI_COMM_WORLD);
            MPI_Reduce(&local_rhs_sq, &global_rhs_sq, 1, MPI_DOUBLE,
                       MPI_SUM, 0, MPI_COMM_WORLD);
            MPI_Reduce(&local_error_sq, &global_error_sq, 1, MPI_DOUBLE,
                       MPI_SUM, 0, MPI_COMM_WORLD);
            MPI_Reduce(&local_exact_sq, &global_exact_sq, 1, MPI_DOUBLE,
                       MPI_SUM, 0, MPI_COMM_WORLD);
            MPI_Reduce(&local_max_error, &global_max_error, 1, MPI_DOUBLE,
                       MPI_MAX, 0, MPI_COMM_WORLD);

            if (rank == 0) {
                const double relative_residual =
                    std::sqrt(global_residual_sq / global_rhs_sq);
                const double relative_error =
                    std::sqrt(global_error_sq / global_exact_sq);
                const bool passed = relative_residual < 1.0e-10 &&
                                    relative_error < 1.0e-10;

                std::cout << std::scientific << std::setprecision(12)
                          << "Reorder time    : " << max_reorder_time << " s\n"
                          << "Factor time     : " << max_factor_time << " s\n"
                          << "Solve time      : " << max_solve_time << " s\n"
                          << "Relative residual ||b-Ax||/||b|| = "
                          << relative_residual << '\n'
                          << "Relative error    ||x-x*||/||x*|| = "
                          << relative_error << '\n'
                          << "Maximum absolute error            = "
                          << global_max_error << '\n'
                          << "========================================\n"
                          << (passed ? "STRUMPACK TEST PASSED" : "STRUMPACK TEST FAILED")
                          << '\n'
                          << "========================================\n";

                if (!passed) {
                    exit_code = EXIT_FAILURE;
                }
            }

            MPI_Bcast(&exit_code, 1, MPI_INT, 0, MPI_COMM_WORLD);
        }
    }  // Destroy solver before shutting down BLACS/MPI.

    strumpack::scalapack::Cblacs_exit(1);
    MPI_Finalize();
    return exit_code;
}
