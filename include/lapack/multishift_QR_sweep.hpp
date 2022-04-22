/// @file multishift_QR_sweep_coloptimized.hpp
/// @author Thijs Steel, KU Leuven, Belgium
//
// Copyright (c) 2013-2022, University of Colorado Denver. All rights reserved.
//
// This file is part of <T>LAPACK.
// <T>LAPACK is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#ifndef __QR_SWEEP_HH__
#define __QR_SWEEP_HH__

#include <memory>
#include <complex>

#include "legacy_api/blas/utils.hpp"
#include "lapack/utils.hpp"
#include "lapack/types.hpp"
#include "lapack/larfg.hpp"
#include "lapack/lahqr_shiftcolumn.hpp"
#include "lapack/move_bulge.hpp"

namespace lapack
{

    template <
        class matrix_t,
        class vector_t,
        enable_if_t<is_complex<type_t<vector_t>>::value, bool> = true,
        enable_if_t<!is_same_v<layout_type<matrix_t>, RowMajor_t>, bool> = true>
    void multishift_QR_sweep(bool want_t, bool want_z, size_type<matrix_t> ilo, size_type<matrix_t> ihi, matrix_t &A, vector_t &s, matrix_t &Z)
    {

        using T = type_t<matrix_t>;
        using real_t = real_type<T>;
        using idx_t = size_type<matrix_t>;
        using pair = std::pair<idx_t, idx_t>;
        using blas::abs;
        using blas::abs1;
        using blas::conj;
        using blas::uroundoff;

        using blas::internal::colmajor_matrix;

        const real_t rzero(0);
        const T one(1);
        const T zero(0);
        const real_t eps = uroundoff<real_t>();
        const idx_t n = ncols(A);

        idx_t n_shifts = size(s);
        idx_t n_bulges = n_shifts / 2;

        const idx_t n_block_desired = n_shifts + n_shifts;

        // Define workspace matrices
        std::unique_ptr<T[]> _V(new T[n_bulges * 3]);
        // V stores the delayed reflectors
        auto V = colmajor_matrix<T>(&_V[0], 3, n_bulges);

        std::unique_ptr<T[]> _U(new T[n_block_desired * n_block_desired]);
        // U stores the accumulated reflectors
        auto U = colmajor_matrix<T>(&_U[0], n_block_desired, n_block_desired);

        std::unique_ptr<T[]> _WH(new T[n_block_desired * n]);
        // WH is a workspace array used for the horizontal multiplications
        auto WH = colmajor_matrix<T>(&_WH[0], n_block_desired, n);

        // WH is a workspace array used for the vertical multiplications
        // This can reuse the WH space in memory, because we will never use it at the same time
        auto WV = colmajor_matrix<T>(&_WH[0], n, n_block_desired);

        // i_pos_block points to the start of the block of bulges
        idx_t i_pos_block;

        //
        // The following code block introduces the bulges into the matrix
        //
        {
            // Near-the-diagonal bulge introduction
            // The calculations are initially limited to the window: A(ilo:ilo+n_block,ilo:ilo+n_block)
            // The rest is updated later via level 3 BLAS
            idx_t n_block = n_block_desired;
            idx_t istart_m = ilo;
            idx_t istop_m = ilo + n_block;
            auto U2 = slice(U, pair{0, n_block}, pair{0, n_block});
            laset(Uplo::General, zero, one, U2);

            for (idx_t i_pos_last = ilo; i_pos_last < n_block - 2; ++i_pos_last)
            {
                // The number of bulges that are in the pencil
                idx_t n_active_bulges = std::min(n_bulges, ((i_pos_last - ilo) / 2) + 1);
                for (idx_t i_bulge = 0; i_bulge < n_active_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    if (i_pos == ilo)
                    {
                        // Introduce bulge
                        T tau;
                        auto H = slice(A, pair{ilo, ilo + 3}, pair{ilo, ilo + 3});
                        lahqr_shiftcolumn(H, v, s[2 * i_bulge], s[2 * i_bulge + 1]);
                        larfg(v, tau);
                        v[0] = tau;
                    }
                    else
                    {
                        // Chase bulge down
                        auto H = slice(A, pair{i_pos - 1, i_pos + 3}, pair{i_pos - 1, i_pos + 2});
                        move_bulge(H, v, s[2 * i_bulge], s[2 * i_bulge + 1]);
                    }

                    // Apply the reflector we just calculated from the right
                    // We leave the last row for later (it interferes with the optimally packed bulges)
                    for (idx_t j = istart_m; j < i_pos + 3; ++j)
                    {
                        auto sum = A(j, i_pos) + v[1] * A(j, i_pos + 1) + v[2] * A(j, i_pos + 2);
                        A(j, i_pos) = A(j, i_pos) - sum * v[0];
                        A(j, i_pos + 1) = A(j, i_pos + 1) - sum * v[0] * conj(v[1]);
                        A(j, i_pos + 2) = A(j, i_pos + 2) - sum * v[0] * conj(v[2]);
                    }

                    // Apply the reflector we just calculated from the left
                    // We only update a single column, the rest is updated later
                    auto sum = A(i_pos, i_pos) + conj(v[1]) * A(i_pos + 1, i_pos) + conj(v[2]) * A(i_pos + 2, i_pos);
                    A(i_pos, i_pos) = A(i_pos, i_pos) - sum * conj(v[0]);
                    A(i_pos + 1, i_pos) = A(i_pos + 1, i_pos) - sum * conj(v[0]) * v[1];
                    A(i_pos + 2, i_pos) = A(i_pos + 2, i_pos) - sum * conj(v[0]) * v[2];


                }

                // The following code performs the delayed update from the left
                // it is optimized for column oriented matrices, but the increased complexity
                // likely causes slower code
                // for (idx_t j = ilo; j < istop_m; ++j)
                // {
                //     idx_t i_bulge_start = (i_pos_last + 2 > j) ? (i_pos_last + 2 - j) / 2 : 0;
                //     for (idx_t i_bulge = i_bulge_start; i_bulge < n_active_bulges; ++i_bulge)
                //     {
                //         idx_t i_pos = i_pos_last - 2 * i_bulge;
                //         auto v = col(V, i_bulge);
                //         auto sum = A(i_pos, j) + conj(v[1]) * A(i_pos + 1, j) + conj(v[2]) * A(i_pos + 2, j);
                //         A(i_pos, j) = A(i_pos, j) - sum * conj(v[0]);
                //         A(i_pos + 1, j) = A(i_pos + 1, j) - sum * conj(v[0]) * v[1];
                //         A(i_pos + 2, j) = A(i_pos + 2, j) - sum * conj(v[0]) * v[2];
                //     }
                // }

                // Delayed update from the left
                for (idx_t i_bulge = 0; i_bulge < n_active_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    for( idx_t j=i_pos+1; j < istop_m; ++j ){
                        auto sum = A(i_pos, j) + conj(v[1]) * A(i_pos + 1, j) + conj(v[2]) * A(i_pos + 2, j);
                        A(i_pos, j) = A(i_pos, j) - sum * conj(v[0]);
                        A(i_pos + 1, j) = A(i_pos + 1, j) - sum * conj(v[0]) * v[1];
                        A(i_pos + 2, j) = A(i_pos + 2, j) - sum * conj(v[0]) * v[2];
                    }
                }

                // Accumulate the reflectors into U
                for (idx_t i_bulge = 0; i_bulge < n_active_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    for (idx_t j = 0; j < nrows(U2); ++j)
                    {
                        auto sum = U2(j, i_pos - ilo) + v[1] * U2(j, i_pos - ilo + 1) + v[2] * U2(j, i_pos - ilo + 2);
                        U2(j, i_pos - ilo) = U2(j, i_pos - ilo) - sum * v[0];
                        U2(j, i_pos - ilo + 1) = U2(j, i_pos - ilo + 1) - sum * v[0] * conj(v[1]);
                        U2(j, i_pos - ilo + 2) = U2(j, i_pos - ilo + 2) - sum * v[0] * conj(v[2]);
                    }
                }
            }
            // Update rest of the matrix
            if (want_t)
            {
                istart_m = 0;
                istop_m = n;
            }
            else
            {
                istart_m = ilo;
                istop_m = ihi;
            }
            // Horizontal multiply
            if (ilo + n_shifts + 1 < istop_m)
            {
                auto A_slice = slice(A, pair{ilo, ilo + n_block}, pair{ilo + n_block, istop_m});
                auto WH_slice = slice(WH, pair{0, nrows(A_slice)}, pair{0, ncols(A_slice)});
                gemm(Op::ConjTrans, Op::NoTrans, one, U2, A_slice, zero, WH_slice);
                lacpy(Uplo::General, WH_slice, A_slice);
            }
            // Vertical multiply
            if (istart_m < ilo)
            {
                auto A_slice = slice(A, pair{istart_m, ilo}, pair{ilo, ilo + n_block});
                auto WV_slice = slice(WV, pair{0, nrows(A_slice)}, pair{0, ncols(A_slice)});
                gemm(Op::NoTrans, Op::NoTrans, one, A_slice, U2, zero, WV_slice);
                lacpy(Uplo::General, WV_slice, A_slice);
            }
            // Update Z (also a vertical multiplication)
            if (want_z)
            {
                auto Z_slice = slice(Z, pair{0, n}, pair{ilo, ilo + n_block});
                auto WV_slice = slice(WV, pair{0, nrows(Z_slice)}, pair{0, ncols(Z_slice)});
                gemm(Op::NoTrans, Op::NoTrans, one, Z_slice, U2, zero, WV_slice);
                lacpy(Uplo::General, WV_slice, Z_slice);
            }

            i_pos_block = n_block - n_shifts;
        }

        //
        // The following code block moves the bulges down untill they are low enough to be removed
        //
        while (i_pos_block < ihi - n_block_desired)
        {

            // Number of positions each bulge will be moved down
            idx_t n_pos = std::min(n_block_desired - n_shifts, ihi - n_shifts - 1 - i_pos_block);
            // Actual blocksize
            idx_t n_block = n_shifts + n_pos;

            auto U2 = slice(U, pair{0, n_block}, pair{0, n_block});
            laset(Uplo::General, zero, one, U2);

            // Near-the-diagonal bulge chase
            // The calculations are initially limited to the window: A(i_pos_block-1:i_pos_block+n_block,i_pos_block:i_pos_block+n_block)
            // The rest is updated later via level 3 BLAS

            idx_t istart_m = i_pos_block;
            idx_t istop_m = i_pos_block + n_block;
            for (idx_t i_pos_last = i_pos_block + n_shifts - 2; i_pos_last < i_pos_block + n_shifts - 2 + n_pos; ++i_pos_last)
            {
                for (idx_t i_bulge = 0; i_bulge < n_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    auto H = slice(A, pair{i_pos - 1, i_pos + 3}, pair{i_pos - 1, i_pos + 2});
                    move_bulge(H, v, s[2 * i_bulge], s[2 * i_bulge + 1]);

                    // Apply the reflector we just calculated from the right
                    // We leave the last row for later (it interferes with the optimally packed bulges)
                    for (idx_t j = istart_m; j < i_pos + 3; ++j)
                    {
                        auto sum = A(j, i_pos) + v[1] * A(j, i_pos + 1) + v[2] * A(j, i_pos + 2);
                        A(j, i_pos) = A(j, i_pos) - sum * v[0];
                        A(j, i_pos + 1) = A(j, i_pos + 1) - sum * v[0] * conj(v[1]);
                        A(j, i_pos + 2) = A(j, i_pos + 2) - sum * v[0] * conj(v[2]);
                    }

                    // Apply the reflector we just calculated from the left
                    // We only update a single column, the rest is updated later
                    auto sum = A(i_pos, i_pos) + conj(v[1]) * A(i_pos + 1, i_pos) + conj(v[2]) * A(i_pos + 2, i_pos);
                    A(i_pos, i_pos) = A(i_pos, i_pos) - sum * conj(v[0]);
                    A(i_pos + 1, i_pos) = A(i_pos + 1, i_pos) - sum * conj(v[0]) * v[1];
                    A(i_pos + 2, i_pos) = A(i_pos + 2, i_pos) - sum * conj(v[0]) * v[2];
                }

                // The following code performs the delayed update from the left
                // it is optimized for column oriented matrices, but the increased complexity
                // likely causes slower code
                // for (idx_t j = i_pos_block; j < istop_m; ++j)
                // {
                //     idx_t i_bulge_start = (i_pos_last + 2 > j) ? (i_pos_last + 2 - j) / 2 : 0;
                //     for (idx_t i_bulge = i_bulge_start; i_bulge < n_bulges; ++i_bulge)
                //     {
                //         idx_t i_pos = i_pos_last - 2 * i_bulge;
                //         auto v = col(V, i_bulge);
                //         auto sum = A(i_pos, j) + conj(v[1]) * A(i_pos + 1, j) + conj(v[2]) * A(i_pos + 2, j);
                //         A(i_pos, j) = A(i_pos, j) - sum * conj(v[0]);
                //         A(i_pos + 1, j) = A(i_pos + 1, j) - sum * conj(v[0]) * v[1];
                //         A(i_pos + 2, j) = A(i_pos + 2, j) - sum * conj(v[0]) * v[2];
                //     }
                // }

                // Delayed update from the left
                for (idx_t i_bulge = 0; i_bulge < n_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    for( idx_t j=i_pos+1; j < istop_m; ++j ){
                        auto sum = A(i_pos, j) + conj(v[1]) * A(i_pos + 1, j) + conj(v[2]) * A(i_pos + 2, j);
                        A(i_pos, j) = A(i_pos, j) - sum * conj(v[0]);
                        A(i_pos + 1, j) = A(i_pos + 1, j) - sum * conj(v[0]) * v[1];
                        A(i_pos + 2, j) = A(i_pos + 2, j) - sum * conj(v[0]) * v[2];
                    }
                }

                // Accumulate the reflectors into U
                for (idx_t i_bulge = 0; i_bulge < n_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    for (idx_t j = 0; j < nrows(U2); ++j)
                    {
                        auto sum = U2(j, i_pos - i_pos_block) + v[1] * U2(j, i_pos - i_pos_block + 1) + v[2] * U2(j, i_pos - i_pos_block + 2);
                        U2(j, i_pos - i_pos_block) = U2(j, i_pos - i_pos_block) - sum * v[0];
                        U2(j, i_pos - i_pos_block + 1) = U2(j, i_pos - i_pos_block + 1) - sum * v[0] * conj(v[1]);
                        U2(j, i_pos - i_pos_block + 2) = U2(j, i_pos - i_pos_block + 2) - sum * v[0] * conj(v[2]);
                    }
                }
            }
            // Update rest of the matrix
            if (want_t)
            {
                istart_m = 0;
                istop_m = n;
            }
            else
            {
                istart_m = ilo;
                istop_m = ihi;
            }
            // Horizontal multiply
            if (i_pos_block + n_block < istop_m)
            {
                auto A_slice = slice(A, pair{i_pos_block, i_pos_block + n_block}, pair{i_pos_block + n_block, istop_m});
                auto WH_slice = slice(WH, pair{0, nrows(A_slice)}, pair{0, ncols(A_slice)});
                gemm(Op::ConjTrans, Op::NoTrans, one, U2, A_slice, zero, WH_slice);
                lacpy(Uplo::General, WH_slice, A_slice);
            }
            // Vertical multiply
            if (istart_m < i_pos_block)
            {
                auto A_slice = slice(A, pair{istart_m, i_pos_block}, pair{i_pos_block, i_pos_block + n_block});
                auto WV_slice = slice(WV, pair{0, nrows(A_slice)}, pair{0, ncols(A_slice)});
                gemm(Op::NoTrans, Op::NoTrans, one, A_slice, U2, zero, WV_slice);
                lacpy(Uplo::General, WV_slice, A_slice);
            }
            // Update Z (also a vertical multiplication)
            if (want_z)
            {
                auto Z_slice = slice(Z, pair{0, n}, pair{i_pos_block, i_pos_block + n_block});
                auto WV_slice = slice(WV, pair{0, nrows(Z_slice)}, pair{0, ncols(Z_slice)});
                gemm(Op::NoTrans, Op::NoTrans, one, Z_slice, U2, zero, WV_slice);
                lacpy(Uplo::General, WV_slice, Z_slice);
            }

            i_pos_block = i_pos_block + n_pos;
        }

        //
        // The following code removes the bulges from the matrix
        //
        {
            idx_t n_block = ihi - i_pos_block;

            auto U2 = slice(U, pair{0, n_block}, pair{0, n_block});
            laset(Uplo::General, zero, one, U2);

            // Near-the-diagonal bulge chase
            // The calculations are initially limited to the window: A(i_pos_block-1:ihi,i_pos_block:ihi)
            // The rest is updated later via level 3 BLAS

            idx_t istart_m = i_pos_block;
            idx_t istop_m = ihi;

            for (idx_t i_pos_last = i_pos_block + n_shifts - 2; i_pos_last < ihi + n_shifts - 1; ++i_pos_last)
            {
                idx_t i_bulge_start = (i_pos_last + 3 > ihi) ? (i_pos_last + 3 - ihi) / 2 : 0;
                for (idx_t i_bulge = i_bulge_start; i_bulge < n_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    if (i_pos == ihi - 2)
                    {
                        // Special case, the bulge is at the bottom, needs a smaller reflector (order 2)
                        auto v = slice(V, pair{0, 2}, i_bulge);
                        auto h = slice(A, pair{i_pos, i_pos + 2}, i_pos - 1);
                        larfg(h, v[0]);
                        v[1] = h[1];
                        h[1] = zero;

                        auto t1 = conj(v[0]);
                        auto v2 = v[1];
                        auto t2 = t1 * v2;
                        // Apply the reflector we just calculated from the right
                        for (idx_t j = istart_m; j < i_pos + 2; ++j)
                        {
                            auto sum = A(j, i_pos) + v2 * A(j, i_pos + 1);
                            A(j, i_pos) = A(j, i_pos) - sum * conj(t1);
                            A(j, i_pos + 1) = A(j, i_pos + 1) - sum * conj(t2);
                        }
                        // Apply the reflector we just calculated from the left
                        for (idx_t j = i_pos; j < istop_m; ++j)
                        {
                            auto sum = A(i_pos, j) + conj(v2) * A(i_pos + 1, j);
                            A(i_pos, j) = A(i_pos, j) - sum * t1;
                            A(i_pos + 1, j) = A(i_pos + 1, j) - sum * t2;
                        }
                        // Accumulate the reflector into U
                        // The loop bounds should be changed to reflect the fact that U2 starts off as diagonal
                        for (idx_t j = 0; j < nrows(U2); ++j)
                        {
                            auto sum = U2(j, i_pos - i_pos_block) + v2 * U2(j, i_pos - i_pos_block + 1);
                            U2(j, i_pos - i_pos_block) = U2(j, i_pos - i_pos_block) - sum * conj(t1);
                            U2(j, i_pos - i_pos_block + 1) = U2(j, i_pos - i_pos_block + 1) - sum * conj(t2);
                        }
                    }
                    else
                    {
                        auto v = col(V, i_bulge);
                        auto H = slice(A, pair{i_pos - 1, i_pos + 3}, pair{i_pos - 1, i_pos + 2});
                        move_bulge(H, v, s[2 * i_bulge], s[2 * i_bulge + 1]);

                        auto t1 = conj(v[0]);
                        auto v2 = v[1];
                        auto t2 = t1 * v2;
                        auto v3 = v[2];
                        auto t3 = t1 * v[2];
                        // Apply the reflector we just calculated from the right (but leave the last row for later)
                        for (idx_t j = istart_m; j < i_pos + 3; ++j)
                        {
                            auto sum = A(j, i_pos) + v2 * A(j, i_pos + 1) + v3 * A(j, i_pos + 2);
                            A(j, i_pos) = A(j, i_pos) - sum * conj(t1);
                            A(j, i_pos + 1) = A(j, i_pos + 1) - sum * conj(t2);
                            A(j, i_pos + 2) = A(j, i_pos + 2) - sum * conj(t3);
                        }

                        // Apply the reflector we just calculated from the left
                        // We only update a single column, the rest is updated later
                        auto sum = A(i_pos, i_pos) + conj(v[1]) * A(i_pos + 1, i_pos) + conj(v[2]) * A(i_pos + 2, i_pos);
                        A(i_pos, i_pos) = A(i_pos, i_pos) - sum * conj(v[0]);
                        A(i_pos + 1, i_pos) = A(i_pos + 1, i_pos) - sum * conj(v[0]) * v[1];
                        A(i_pos + 2, i_pos) = A(i_pos + 2, i_pos) - sum * conj(v[0]) * v[2];
                    }
                }

                i_bulge_start = (i_pos_last + 4 > ihi) ? (i_pos_last + 4 - ihi) / 2 : 0;

                // The following code performs the delayed update from the left
                // it is optimized for column oriented matrices, but the increased complexity
                // likely causes slower code
                // for (idx_t j = i_pos_block; j < istop_m; ++j)
                // {
                //     idx_t i_bulge_start2 = (i_pos_last + 2 > j) ? (i_pos_last + 2 - j) / 2 : 0;
                //     i_bulge_start2 = std::max(i_bulge_start,i_bulge_start2);
                //     for (idx_t i_bulge = i_bulge_start2; i_bulge < n_bulges; ++i_bulge)
                //     {
                //         idx_t i_pos = i_pos_last - 2 * i_bulge;
                //         auto v = col(V, i_bulge);
                //         auto sum = A(i_pos, j) + conj(v[1]) * A(i_pos + 1, j) + conj(v[2]) * A(i_pos + 2, j);
                //         A(i_pos, j) = A(i_pos, j) - sum * conj(v[0]);
                //         A(i_pos + 1, j) = A(i_pos + 1, j) - sum * conj(v[0]) * v[1];
                //         A(i_pos + 2, j) = A(i_pos + 2, j) - sum * conj(v[0]) * v[2];
                //     }
                // }

                // Delayed update from the left
                for (idx_t i_bulge = i_bulge_start; i_bulge < n_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    for (idx_t j = i_pos + 1; j < istop_m; ++j)
                    {
                        auto sum = A(i_pos, j) + conj(v[1]) * A(i_pos + 1, j) + conj(v[2]) * A(i_pos + 2, j);
                        A(i_pos, j) = A(i_pos, j) - sum * conj(v[0]);
                        A(i_pos + 1, j) = A(i_pos + 1, j) - sum * conj(v[0]) * v[1];
                        A(i_pos + 2, j) = A(i_pos + 2, j) - sum * conj(v[0]) * v[2];
                    }
                }

                // Accumulate the reflectors into U
                for (idx_t i_bulge = i_bulge_start; i_bulge < n_bulges; ++i_bulge)
                {
                    idx_t i_pos = i_pos_last - 2 * i_bulge;
                    auto v = col(V, i_bulge);
                    for (idx_t j = 0; j < nrows(U2); ++j)
                    {
                        auto sum = U2(j, i_pos - i_pos_block) + v[1] * U2(j, i_pos - i_pos_block + 1) + v[2] * U2(j, i_pos - i_pos_block + 2);
                        U2(j, i_pos - i_pos_block) = U2(j, i_pos - i_pos_block) - sum * v[0];
                        U2(j, i_pos - i_pos_block + 1) = U2(j, i_pos - i_pos_block + 1) - sum * v[0] * conj(v[1]);
                        U2(j, i_pos - i_pos_block + 2) = U2(j, i_pos - i_pos_block + 2) - sum * v[0] * conj(v[2]);
                    }
                }
            }

            // Update rest of the matrix
            if (want_t)
            {
                istart_m = 0;
                istop_m = n;
            }
            else
            {
                istart_m = ilo;
                istop_m = ihi;
            }
            // Horizontal multiply
            if (ihi < istop_m)
            {
                auto A_slice = slice(A, pair{i_pos_block, ihi}, pair{ihi, istop_m});
                auto WH_slice = slice(WH, pair{0, nrows(A_slice)}, pair{0, ncols(A_slice)});
                gemm(Op::ConjTrans, Op::NoTrans, one, U2, A_slice, zero, WH_slice);
                lacpy(Uplo::General, WH_slice, A_slice);
            }
            // Vertical multiply
            if (istart_m < i_pos_block)
            {
                auto A_slice = slice(A, pair{istart_m, i_pos_block}, pair{i_pos_block, ihi});
                auto WV_slice = slice(WV, pair{0, nrows(A_slice)}, pair{0, ncols(A_slice)});
                gemm(Op::NoTrans, Op::NoTrans, one, A_slice, U2, zero, WV_slice);
                lacpy(Uplo::General, WV_slice, A_slice);
            }
            // Update Z (also a vertical multiplication)
            if (want_z)
            {
                auto Z_slice = slice(Z, pair{0, n}, pair{i_pos_block, ihi});
                auto WV_slice = slice(WV, pair{0, nrows(Z_slice)}, pair{0, ncols(Z_slice)});
                gemm(Op::NoTrans, Op::NoTrans, one, Z_slice, U2, zero, WV_slice);
                lacpy(Uplo::General, WV_slice, Z_slice);
            }
        }
    }

} // lapack

#endif // __QR_SWEEP_HH__